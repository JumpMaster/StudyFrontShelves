#define FASTLED_ESP32_FLASH_LOCK 0

#include <bitswap.h>
#include <chipsets.h>
#include <color.h>
#include <colorpalettes.h>
#include <colorutils.h>
#include <controller.h>
#include <cpp_compat.h>
#include <dmx.h>
#include <fastled_config.h>
#include <fastled_delay.h>
#include <fastled_progmem.h>
#include <FastLED.h>
#include <fastpin.h>
#include <fastspi_bitbang.h>
#include <fastspi_dma.h>
#include <fastspi_nop.h>
#include <fastspi_ref.h>
#include <fastspi_types.h>
#include <fastspi.h>
#include <hsv2rgb.h>
#include <led_sysdefs.h>
#include <lib8tion.h>
#include <noise.h>
#include <pixelset.h>
#include <pixeltypes.h>
#include <platforms.h>
#include <power_mgt.h>

#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "PapertrailLogger.h"
#include "secrets.h"

// Stubs
void mqttCallback(char* topic, byte* payload, unsigned int length);
uint32_t Wheel(byte WheelPos);
// void RainbowCycleUpdate(Adafruit_NeoPixel *pixels, uint8_t index);

bool isDebug = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long wifiReconnectPreviousMillis = 0;
unsigned long wifiReconnectInterval = 30000;

PapertrailLogger *infoLog;

const gpio_num_t relayPin = GPIO_NUM_26;
const gpio_num_t shelf1Pin = GPIO_NUM_14;
const gpio_num_t shelf2Pin = GPIO_NUM_32;
const gpio_num_t shelf3Pin = GPIO_NUM_15;
const gpio_num_t shelf4Pin = GPIO_NUM_33;
const gpio_num_t shelf5Pin = GPIO_NUM_27;
const gpio_num_t shelf6Pin = GPIO_NUM_12;
const gpio_num_t shelf7Pin = GPIO_NUM_13;

const uint8_t fps = 60;
uint32_t nextRun = 0;

typedef enum {
    LIGHT_EFFECT_NONE = 0,
    LIGHT_EFFECT_RAINBOW = 1
} LightEffect;

struct ShelfData {
    const gpio_num_t pin;
    const uint8_t numLeds;
    bool enabled;
    bool active;
    uint8_t effect;
    uint8_t targetBrightness;
    uint8_t brightness;
    uint8_t color[3];
};

const uint8_t shelfCount = 7;

ShelfData shelfData[shelfCount] = {
    { GPIO_NUM_14, 95, false, false, LIGHT_EFFECT_NONE, 255, 0, {255, 255, 255} },
    { GPIO_NUM_32, 95, false, false, LIGHT_EFFECT_NONE, 255, 0, {255, 255, 255} },
    { GPIO_NUM_15, 96, false, false, LIGHT_EFFECT_NONE, 255, 0, {255, 255, 255} },
    { GPIO_NUM_33, 82, false, false, LIGHT_EFFECT_NONE, 255, 0, {255, 255, 255} },
    { GPIO_NUM_27, 82, false, false, LIGHT_EFFECT_NONE, 255, 0, {255, 255, 255} },
    { GPIO_NUM_12, 35, false, false, LIGHT_EFFECT_NONE, 255, 0, {255, 255, 255} },
    { GPIO_NUM_13, 40, false, false, LIGHT_EFFECT_NONE, 255, 0, {255, 255, 255} }
};

CRGBArray<95> shelf1;
CRGBArray<95> shelf2;
CRGBArray<96> shelf3;
CRGBArray<82> shelf4;
CRGBArray<82> shelf5;
CRGBArray<35> shelf6;
CRGBArray<40> shelf7;

struct CRGB *shelves[] ={shelf1, shelf2, shelf3, shelf4, shelf5, shelf6, shelf7}; 


bool psuShouldBeEnabled = false;
bool psuEnabled = false;
bool psuReady = false;
uint32_t psuActionableTime = 0;
const uint16_t psuShutdownBuffer = 10000;
const uint16_t psuStartupBuffer = 100;

uint32_t nextMetricsUpdate = 0;

uint32_t getMillis()
{
    return esp_timer_get_time() / 1000;
}

void sendTelegrafMetrics()
{
    uint32_t uptime = getMillis() / 1000;

    char buffer[150];

    snprintf(buffer, sizeof(buffer),
        "status,device=%s uptime=%d,resetReason=%d,firmware=\"%s\",memFree=%ld,memTotal=%ld",
        deviceName,
        uptime,
        esp_reset_reason(),
        esp_get_idf_version(),
        ESP.getFreeHeap(),
        ESP.getHeapSize());
    mqttClient.publish("telegraf/particle", buffer);
}

void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    char p[length + 1];
    memcpy(p, payload, length);
    p[length] = '\0';


    if (isDebug)
    {
        char buffer[100];
        snprintf(buffer, 100, "%s - %s", topic, p);
        infoLog->println(buffer);
    }

    uint8_t topicLen = strlen(topic);
    
    char isSet[4];
    memcpy(isSet, &topic[topicLen-3], 3);
    isSet[3] = '\0';

    if (strcmp(isSet, "set") != 0)
        return;

    char command[10];
    memcpy(command, &topic[31], topicLen-35);
    command[topicLen-35] = '\0';

    int8_t light = -1;
    if (strlen(topic) > 29 && strncmp(topic, "home/study/light/front-shelf/", 29) == 0) 
    {
        light = topic[29] - '0' - 1;
    }

    if (light < 0)
        return;

    if (strcmp(command, "switch") == 0)
    {
        bool state = strcmp(p, "ON") == 0;

        if (shelfData[light].enabled == state)
            return;
        else
            shelfData[light].enabled = state;
    
        bool psuEnabledCheck = false;
        for (uint8_t i = 0; i < shelfCount; i++) {
            if (shelfData[i].enabled)
                psuEnabledCheck = true;
        }
        psuShouldBeEnabled = psuEnabledCheck;
    }
    else if (strcmp(command, "brightness") == 0)
    {
        uint8_t brightness;
        sscanf(p, "%d", &brightness);
        shelfData[light].targetBrightness = brightness;
    }
    else if (strcmp(command, "rgb") == 0)
    {
        char * token = strtok(p, ",");

        for (int i = 0; i < 3; i++) {
            shelfData[light].color[i] = atoi(token);
            token = strtok(NULL, ",");
        }
    }
    else if (strcmp(command, "effect") == 0)
    {
        if (strcmp(p, "Rainbow") == 0)
            shelfData[light].effect = LIGHT_EFFECT_RAINBOW;
        else
            shelfData[light].effect = LIGHT_EFFECT_NONE;
    }

    char bufferTopic[50];
    char bufferPayload[15];
    snprintf(bufferTopic, sizeof(bufferTopic), "home/study/light/front-shelf/%d/switch", light+1);
    mqttClient.publish(bufferTopic, shelfData[light].enabled ? "ON" : "OFF");
    snprintf(bufferTopic, sizeof(bufferTopic), "home/study/light/front-shelf/%d/brightness", light+1);
    snprintf(bufferPayload, sizeof(bufferPayload), "%d", shelfData[light].targetBrightness);
    mqttClient.publish(bufferTopic, bufferPayload);
    snprintf(bufferTopic, sizeof(bufferTopic), "home/study/light/front-shelf/%d/rgb", light+1);
    snprintf(bufferPayload, sizeof(bufferPayload), "%d,%d,%d", shelfData[light].color[0], shelfData[light].color[1], shelfData[light].color[2]);
    mqttClient.publish(bufferTopic, bufferPayload);
    snprintf(bufferTopic, sizeof(bufferTopic), "home/study/light/front-shelf/%d/effect", light+1);
    mqttClient.publish(bufferTopic, shelfData[light].effect == LIGHT_EFFECT_RAINBOW ? "Rainbow" : "None");
}

void mqttConnect()
{
    // Loop until we're reconnected
    while (!mqttClient.connected())
    {
        infoLog->println("Connecting to MQTT");
        // Attempt to connect
        if (mqttClient.connect(deviceName, mqtt_username, mqtt_password))
        {
            infoLog->println("Connected to MQTT");

            mqttClient.subscribe("home/study/light/front-shelf/+/#");
            
            char buffer[40];
            for (int i = 0; i < shelfCount; i++) {
                snprintf(buffer, sizeof(buffer), "home/study/light/front-shelf/%d/switch", i+1);
                mqttClient.publish(buffer, shelfData[i].enabled ? "ON" : "OFF");
            }
        }
        else
        {
            infoLog->println("Failed to connect to MQTT");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void connectToNetwork()
{
    WiFi.begin(ssid, password);
 
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
    }

    if (WiFi.status() == WL_CONNECTED)
        infoLog->println("Connected to WiFi");
}

void setup()
{
    gpio_set_direction(relayPin, GPIO_MODE_OUTPUT); // RELAY

    for (int i = 0; i < shelfCount; i++)
        gpio_set_direction(shelfData[i].pin, GPIO_MODE_OUTPUT);

    gpio_set_level(relayPin, LOW); // LOW = OFF, HIGH = ON

    uint8_t chipid[6];
    esp_read_mac(chipid, ESP_MAC_WIFI_STA);
    char macAddress[19];
    sprintf(macAddress, "%02x:%02x:%02x:%02x:%02x:%02x",chipid[0], chipid[1], chipid[2], chipid[3], chipid[4], chipid[5]);

    infoLog = new PapertrailLogger(papertrailAddress, papertrailPort, LogLevel::Info, macAddress, deviceName);

    connectToNetwork();

    // ArduinoOTA.begin(WiFi.localIP(), "Arduino", "password", InternalStorage);
    ArduinoOTA.setHostname(deviceName);
    ArduinoOTA.begin();

    mqttClient.setBufferSize(2048);

    mqttClient.setServer(mqtt_server, 1883);
    mqttClient.setCallback(mqttCallback);



    // for (int i = 0; i < shelfCount; i++)
    // {
        // FastLED.addLeds<NEOPIXEL, shelfData[i].pin>(shelves[i], shelfData[i].numLeds);
        // FastLED.addLeds<NEOPIXEL, 32>(shelves[0], 60);

        //shelfController[0] = &
        FastLED.addLeds<WS2813, shelf1Pin, RGB>(shelf1, shelfData[0].numLeds);
        //shelfController[1] = &
        FastLED.addLeds<WS2813, shelf2Pin, RGB>(shelf2, shelfData[1].numLeds);
        //shelfController[2] = &
        FastLED.addLeds<WS2813, shelf3Pin, RGB>(shelf3, shelfData[2].numLeds);
        //shelfController[3] = &
        FastLED.addLeds<WS2813, shelf4Pin, RGB>(shelf4, shelfData[3].numLeds);
        //shelfController[4] = &
        FastLED.addLeds<WS2813, shelf5Pin, RGB>(shelf5, shelfData[4].numLeds);
        //shelfController[5] = &
        FastLED.addLeds<WS2813, shelf6Pin, RGB>(shelf6, shelfData[5].numLeds);
        //shelfController[6] = &
        FastLED.addLeds<WS2813, shelf7Pin, RGB>(shelf7, shelfData[6].numLeds);

    // }

/*
    for (uint8_t i = 0; i < shelfCount; i++)
    {
        shelves[i].begin();
        shelves[i].clear();
        shelves[i].show();
    }
*/
}

void loop()
{
    unsigned long currentMillis = getMillis();
    ArduinoOTA.handle();

    if (!mqttClient.connected())
    {
        mqttConnect();
    }
    else
    {
        mqttClient.loop();
        
        if (currentMillis > nextMetricsUpdate)
        {
            sendTelegrafMetrics();
            nextMetricsUpdate = currentMillis + 30000;
        }
    }

    // if WiFi is down, try reconnecting
    if ((WiFi.status() != WL_CONNECTED) && (currentMillis - wifiReconnectPreviousMillis >= wifiReconnectInterval))
    {
        // Serial.print(millis());
        // Serial.println("Reconnecting to WiFi...");
        WiFi.disconnect();
        WiFi.reconnect();

        if (WiFi.status() == WL_CONNECTED)
            infoLog->println("Reconnected to WiFi");

        wifiReconnectPreviousMillis = currentMillis;
    }

    static uint8_t index;
    
    if (psuShouldBeEnabled && !psuReady)
    {
        if (!psuEnabled)
        {
            gpio_set_level(relayPin, HIGH); // LOW = OFF, HIGH = ON
            psuEnabled = true;
            psuReady = false;
            psuActionableTime = currentMillis + psuStartupBuffer;
        }
        else if (currentMillis > psuActionableTime)
        {
            psuReady = true;
            psuActionableTime = 0;
        }
    }

    if (!psuShouldBeEnabled && psuEnabled)
    {
        if (psuActionableTime == 0)
        {
            psuActionableTime = currentMillis + psuShutdownBuffer;
        }
        else if (currentMillis > psuActionableTime)
        {
            psuActionableTime = 0;
            gpio_set_level(relayPin, LOW); // LOW = OFF, HIGH = ON
            psuReady = false;
            psuEnabled = false;
        }
    }

    if (psuShouldBeEnabled && psuReady && psuActionableTime != 0)
        psuActionableTime = 0;

    if (psuReady && currentMillis > nextRun)
    {
        index--; // Keep it moving
        for (int i = 0; i < shelfCount; i++)
        {
            if (!shelfData[i].enabled && !shelfData[i].active && shelfData[i].brightness == 0)
                continue;

            if (shelfData[i].enabled && !shelfData[i].active)
            {
                shelfData[i].active = true;
                // shelves[i].begin();
                shelfData[i].brightness = 0;
                // shelves[i].clear();
            }

            if (shelfData[i].enabled && shelfData[i].brightness != shelfData[i].targetBrightness)
            {
                if (shelfData[i].brightness <= (shelfData[i].targetBrightness-5))
                    shelfData[i].brightness += 5;
                else if (shelfData[i].targetBrightness <= (shelfData[i].brightness-5))
                    shelfData[i].brightness -= 5;
                else
                    shelfData[i].brightness = shelfData[i].targetBrightness;
                
                // shelves[i].setBrightness(shelfData[i].brightness);
                // shelfController[i].setBrightness(shelfData[i].brightness);
                FastLED.setBrightness(shelfData[0].brightness);
            }
            else if (!shelfData[i].enabled && shelfData[i].brightness != 0)
            {
                if (shelfData[i].brightness >= 5)
                    shelfData[i].brightness -= 5;
                else
                    shelfData[i].brightness = 0;
                // shelfController[i].setBrightness(shelfData[i].brightness);
                FastLED.setBrightness(shelfData[0].brightness);
            }
            else if (!shelfData[i].enabled && shelfData[i].brightness == 0 && shelfData[i].active)
            {
                shelfData[i].active = false;
            }

            if (shelfData[i].effect == LIGHT_EFFECT_RAINBOW)
            {
                // RainbowCycleUpdate(&shelves[i], index);
            }
            else if (shelfData[i].effect == LIGHT_EFFECT_NONE)
            {
                fill_solid(shelves[i], shelfData[i].numLeds, CRGB(shelfData[i].color[0],shelfData[i].color[1],shelfData[i].color[2])); //8 is number of elements in the array
                // for (uint8_t j÷ = 0; j < shelves[i].numPixels(); j++) {
                    // shelves[i].setPixelColor(j, shelfData[i].color[1], shelfData[i].color[0], shelfData[i].color[2]);
                // }
            }
            //FastLED[i].showLeds(255);//shelfData[i].brightness);
            //shelves[i].show();
        }
        FastLED.show();
        nextRun = millis() + (1000/fps);
    }
}

// Update the Rainbow Cycle Pattern
/*
void RainbowCycleUpdate(Adafruit_NeoPixel *pixels, uint8_t index)
{
    for(int i=0; i< pixels->numPixels(); i++)
    {
        // pixels->setPixelColor(i, Wheel(((i * 256 / pixels->numPixels()) + index) & 255));
    }
}

uint32_t Wheel(byte WheelPos)
{
    WheelPos = 255 - WheelPos;
    if(WheelPos < 85)
    {
        // return shelves[0].Color(255 - WheelPos * 3, 0, WheelPos * 3);
        return 0;
    }
    else if(WheelPos < 170)
    {
        WheelPos -= 85;
        // return shelves[0].Color(0, WheelPos * 3, 255 - WheelPos * 3);
        return 0;
    }
    else
    {
        WheelPos -= 170;
        // return shelves[0].Color(WheelPos * 3, 255 - WheelPos * 3, 0);
        return 0;
    }
}
*/