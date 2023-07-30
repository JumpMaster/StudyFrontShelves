#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <PubSubClient.h>
#include "PapertrailLogger.h"
#include "secrets.h"

// Stubs
void mqttCallback(char* topic, byte* payload, unsigned int length);
uint32_t Wheel(byte WheelPos);
//void RainbowCycleUpdate(Adafruit_NeoPixel *pixels, uint8_t index);

bool isDebug = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long wifiReconnectPreviousMillis = 0;
unsigned long wifiReconnectInterval = 30000;

PapertrailLogger *infoLog;

const gpio_num_t relayPin = GPIO_NUM_26;

const uint8_t fps = 120;
uint32_t nextRun = 0;

bool showFPS = false;
uint32_t nextFPSCount;
uint32_t fpsCount;

typedef enum
{
    LIGHT_EFFECT_SOLID = 0,
    LIGHT_EFFECT_RAINBOW = 1,
    LIGHT_EFFECT_HENRY = 2,
} LightEffect;

struct ShelfData
{
    const gpio_num_t dataPin;
    const uint8_t numLeds;
    bool enabled;
    bool active;
    uint8_t effect;
    uint8_t targetBrightness;
    uint8_t brightness;
    uint32_t color;
};

const uint8_t shelfCount = 7;

ShelfData shelfData[shelfCount] =
{
    { GPIO_NUM_14, 95, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215},
    { GPIO_NUM_32, 95, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215},
    { GPIO_NUM_15, 96, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215},
    { GPIO_NUM_33, 82, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215},
    { GPIO_NUM_27, 82, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215},
    { GPIO_NUM_12, 35, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215},
    { GPIO_NUM_13, 40, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215}
};

Adafruit_NeoPixel shelves[shelfCount] =
{
    Adafruit_NeoPixel(shelfData[0].numLeds, shelfData[0].dataPin, NEO_RGB + NEO_KHZ800),
    Adafruit_NeoPixel(shelfData[1].numLeds, shelfData[1].dataPin, NEO_RGB + NEO_KHZ800),
    Adafruit_NeoPixel(shelfData[2].numLeds, shelfData[2].dataPin, NEO_RGB + NEO_KHZ800),
    Adafruit_NeoPixel(shelfData[3].numLeds, shelfData[3].dataPin, NEO_RGB + NEO_KHZ800),
    Adafruit_NeoPixel(shelfData[4].numLeds, shelfData[4].dataPin, NEO_RGB + NEO_KHZ800),
    Adafruit_NeoPixel(shelfData[5].numLeds, shelfData[5].dataPin, NEO_RGB + NEO_KHZ800),
    Adafruit_NeoPixel(shelfData[6].numLeds, shelfData[6].dataPin, NEO_RGB + NEO_KHZ800)
};

                // RED, YELLOW, GREEN, BLUE, PURPLE
const uint32_t HenryColors[] = {16711680, 16776960, 65280, 255, 16711935};
uint8_t HenryColor = 0;
bool HenryColorOrBlack = false;
uint8_t HenryLedNumber = 0;

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
        "status,device=%s uptime=%d,resetReason=%d,firmware=\"%s\",memUsed=%ld,memTotal=%ld",
        deviceName,
        uptime,
        esp_reset_reason(),
        esp_get_idf_version(),
        ESP.getHeapSize()-ESP.getFreeHeap(),
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

    char command[11];
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

        uint8_t c[3];
        for (int i = 0; i < 3; i++) {
            c[i] = atoi(token);
            token = strtok(NULL, ",");
        }
        shelfData[light].color = (c[0] << 16) | (c[1] << 8) | c[2];
        if (light == 5) { shelfData[6].color = shelfData[5].color; }
    }
    else if (strcmp(command, "effect") == 0)
    {
        if (strcmp(p, "Rainbow") == 0)
            shelfData[light].effect = LIGHT_EFFECT_RAINBOW;
        else if (strcmp(p, "Henry") == 0)
        {
            shelfData[light].effect = LIGHT_EFFECT_HENRY;
            HenryColor = 0;
            HenryColorOrBlack = false;
            HenryLedNumber = 0;
        }
        else
            shelfData[light].effect = LIGHT_EFFECT_SOLID;
    }

    char bufferTopic[50];
    char bufferPayload[15];
    snprintf(bufferTopic, sizeof(bufferTopic), "home/study/light/front-shelf/%d/switch", light+1);
    mqttClient.publish(bufferTopic, shelfData[light].enabled ? "ON" : "OFF");
    snprintf(bufferTopic, sizeof(bufferTopic), "home/study/light/front-shelf/%d/brightness", light+1);
    snprintf(bufferPayload, sizeof(bufferPayload), "%d", shelfData[light].targetBrightness);
    mqttClient.publish(bufferTopic, bufferPayload);
    snprintf(bufferTopic, sizeof(bufferTopic), "home/study/light/front-shelf/%d/rgb", light+1);
    snprintf(bufferPayload, sizeof(bufferPayload), "%d,%d,%d", (shelfData[light].color >> 16) & 0xff, (shelfData[light].color >> 8) & 0xff, shelfData[light].color & 0xff);
    mqttClient.publish(bufferTopic, bufferPayload);
    snprintf(bufferTopic, sizeof(bufferTopic), "home/study/light/front-shelf/%d/effect", light+1);
    mqttClient.publish(bufferTopic, shelfData[light].effect == LIGHT_EFFECT_RAINBOW ? "Rainbow" : shelfData[light].effect == LIGHT_EFFECT_HENRY ? "Henry" : "Solid");
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

            mqttClient.subscribe("home/study/light/front-shelf/+/+/set");
            
            char buffer[40];
            for (int i = 0; i < shelfCount; i++)
            {
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

void fadeyShelfy(uint8_t shelf)
{
    uint32_t targetColor = shelfData[shelf].color;
    uint8_t targetR = (targetColor >> 16) & 0xff; // red
    uint8_t targetG = (targetColor >> 8) & 0xff; // green
    uint8_t targetB = targetColor & 0xff; // blue

    for (int j = 0; j < shelfData[shelf].numLeds; j++)
    {
        uint32_t currentColor = shelves[shelf].getPixelColor(j);
        if (currentColor != targetColor)
        {
            uint8_t currentR = (currentColor >> 16) & 0xff; // red
            uint8_t currentG = (currentColor >> 8) & 0xff; // green
            uint8_t currentB = currentColor & 0xff; // blue

            targetR = currentR >= targetR+5 ? currentR-5 : (currentR <= targetR-5 ? currentR+5 : targetR);
            targetG = currentG >= targetG+5 ? currentG-5 : (currentG <= targetG-5 ? currentG+5 : targetG);
            targetB = currentB >= targetB+5 ? currentB-5 : (currentB <= targetB-5 ? currentB+5 : targetB);

            targetColor = shelves[shelf].Color(targetR, targetG, targetB);

            shelves[shelf].setPixelColor(j, targetColor);
        }
    }
}

void setup()
{
    gpio_set_direction(relayPin, GPIO_MODE_OUTPUT); // RELAY

    for (uint8_t i =0; i < shelfCount; i++)
        gpio_set_direction(shelfData[0].dataPin, GPIO_MODE_OUTPUT);

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

    for (uint8_t i = 0; i < shelfCount; i++)
    {
        shelves[i].begin();
        shelves[i].clear();
        shelves[i].show();
    }
}

void loop()
{
    unsigned long currentMillis = getMillis();
    static uint16_t index;
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
        Serial.print(currentMillis);
        Serial.println("Reconnecting to WiFi...");
        WiFi.disconnect();
        WiFi.reconnect();

        if (WiFi.status() == WL_CONNECTED)
            infoLog->println("Reconnected to WiFi");

        wifiReconnectPreviousMillis = currentMillis;
    }
    
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
        index += 100; // Keep it moving

        if (showFPS) {
            fpsCount++;

            if (currentMillis > nextFPSCount) {
                nextFPSCount = currentMillis + 1000;
                char buffer[10];
                snprintf(buffer, 10, "%d", fpsCount);
                mqttClient.publish("log/fps", buffer);
                fpsCount = 0;
            }
        }

        for (int i = 0; i < shelfCount-1; i++) // shelfCount-1 as I'm bodging a combination of shelves 6 and 7.
        {
            if (!shelfData[i].enabled && !shelfData[i].active && shelfData[i].brightness == 0)
                continue;

            if (shelfData[i].enabled && !shelfData[i].active)
            {
                shelfData[i].active = true;
                shelfData[i].brightness = 0;
                shelves[i].begin();
                shelves[i].clear();
                //shelves[i].fill(shelfData[i].color, 0, shelves[i].numPixels());
                        
                if (i == 5)
                {
                    shelves[6].begin();
                    shelves[6].clear();
                    //shelves[i+1].fill(shelfData[i].color, 0, shelves[i+1].numPixels());
                }
            }

            if (shelfData[i].enabled && shelfData[i].brightness != shelfData[i].targetBrightness)
            {
                if (shelfData[i].brightness <= (shelfData[i].targetBrightness-5))
                    shelfData[i].brightness += 5;
                else if (shelfData[i].targetBrightness <= (shelfData[i].brightness-5))
                    shelfData[i].brightness -= 5;
                else
                    shelfData[i].brightness = shelfData[i].targetBrightness;
                
                shelves[i].setBrightness(shelfData[i].brightness);
                if (i == 5)
                    shelves[i+1].setBrightness(shelfData[i].brightness);
            }
            else if (!shelfData[i].enabled && shelfData[i].brightness != 0)
            {
                if (shelfData[i].brightness >= 5)
                    shelfData[i].brightness -= 5;
                else
                    shelfData[i].brightness = 0;
                shelves[i].setBrightness(shelfData[i].brightness);
                if (i == 5)
                {
                    shelves[i+1].setBrightness(shelfData[i].brightness);
                }
            }
            else if (!shelfData[i].enabled && shelfData[i].brightness == 0 && shelfData[i].active)
            {
                shelfData[i].active = false;
            }

            if (shelfData[i].effect == LIGHT_EFFECT_RAINBOW)
            {                
                uint8_t saturation = 255;
                uint8_t brightness = 255;
                uint8_t numLeds;
                numLeds = shelves[i].numPixels();
                if (i == 5)
                    numLeds += shelves[i+1].numPixels();

                for (uint16_t j=0; j < numLeds; j++)
                {
                    uint16_t hue = index + (j * 65536) / numLeds;
                    uint32_t color = shelves[i].ColorHSV(hue, saturation, brightness);
                    color = shelves[i].gamma32(color);

                    if (i == 5)
                    {
                        if (j < shelves[i].numPixels())
                            shelves[i].setPixelColor((shelves[i].numPixels()-1) - j, color);
                        else
                            shelves[i+1].setPixelColor(j - shelves[i].numPixels(), color);
                    }
                    else
                        shelves[i].setPixelColor(j, color);
                }
                
            }
            else if (shelfData[i].effect == LIGHT_EFFECT_HENRY)
            {
                if (i < 5 && HenryLedNumber < shelfData[i].numLeds)
                {
                    shelves[i].setPixelColor(HenryLedNumber, HenryColorOrBlack ? HenryColors[HenryColor] : 0);

                    if (i == 5)
                        shelves[i+1].setPixelColor(HenryLedNumber, HenryColorOrBlack ? HenryColors[HenryColor] : 0);
                }
                else if (i == 5 && HenryLedNumber < shelfData[i].numLeds + shelfData[i+1].numLeds)
                {
                    if (HenryLedNumber < shelves[i].numPixels())
                        shelves[i].setPixelColor((shelves[i].numPixels()-1) - HenryLedNumber, HenryColorOrBlack ? HenryColors[HenryColor] : 0);
                    else
                        shelves[i+1].setPixelColor(HenryLedNumber - shelves[i].numPixels(), HenryColorOrBlack ? HenryColors[HenryColor] : 0);
                }

                if (i == 5)
                {
                    if (HenryLedNumber >= 96)
                    {
                        HenryLedNumber = 0;
                        HenryColorOrBlack = !HenryColorOrBlack;
                        if (!HenryColorOrBlack)
                            HenryColor = HenryColor == 4 ? 0 : HenryColor + 1;
                    }
                    else
                    {
                        HenryLedNumber++;
                    }
                }
            }
            else if (shelfData[i].effect == LIGHT_EFFECT_SOLID)
            {
                fadeyShelfy(i);
                if (i == 5)
                    fadeyShelfy(6);

                /*
                if (shelves[i].getPixelColor(0) != shelfData[i].color)
                {
                    uint32_t targetColor;

                    uint8_t targetR = (shelfData[i].color >> 16) & 0xff; // red
                    uint8_t targetG = (shelfData[i].color >> 8) & 0xff; // green
                    uint8_t targetB = shelfData[i].color & 0xff; // blue

                    uint8_t currentR = (shelves[i].getPixelColor(0) >> 16) & 0xff; // red
                    uint8_t currentG = (shelves[i].getPixelColor(0) >> 8) & 0xff; // green
                    uint8_t currentB = shelves[i].getPixelColor(0) & 0xff; // blue

                    targetR = currentR > targetR+1 ? currentR-2 : (currentR < targetR-1 ? currentR+2 : targetR);
                    targetG = currentG > targetG+1 ? currentG-2 : (currentG < targetG-1 ? currentG+2 : targetG);
                    targetB = currentB > targetB+1 ? currentB-2 : (currentB < targetB-1 ? currentB+2 : targetB);

                    targetColor = shelves[i].Color(targetR, targetG, targetB);

                    shelves[i].fill(targetColor, 0, shelves[i].numPixels());
                    if (i == 5)
                        shelves[i+1].fill(targetColor, 0, shelves[i+1].numPixels());
                }
                */
                /*
                shelves[i].fill(shelfData[i].color, 0, shelves[i].numPixels());
                if (i == 5)
                    shelves[i+1].fill(shelfData[i].color, 0, shelves[i+1].numPixels());
                */
            }
            shelves[i].show();
            if (i == 5)
                shelves[i+1].show();
        }
        nextRun = currentMillis + (1000/fps);
    }
}
