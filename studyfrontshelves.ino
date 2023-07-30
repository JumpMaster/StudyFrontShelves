#include "studyfrontshelves.h"

void connectToNetwork()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPassword);

    if (WiFi.waitForConnectResult() == WL_CONNECTED && wifiReconnectCount == 0)
        Log.println("Connected to WiFi");
}

void buildLedMapping()
{
    for (uint16_t i = 0; i < stripData[0].numLeds; i++)
        stripMapping[0][i] = (NUM_LEDS * 6) + i;

    for (uint16_t i = 0; i < stripData[1].numLeds; i++)
        stripMapping[1][i] = (NUM_LEDS * 5) + i;

    for (uint16_t i = 0; i < stripData[2].numLeds; i++)
        stripMapping[2][i] = (NUM_LEDS * 4) + i;
    
    for (uint16_t i = 0; i < stripData[3].numLeds; i++)
        stripMapping[3][i] = (NUM_LEDS * 3) + i;
    
    for (uint16_t i = 0; i < stripData[4].numLeds; i++)
        stripMapping[4][i] = (NUM_LEDS * 2) + i;

    for (uint16_t i = 0; i < 35; i++)
        stripMapping[5][i] = (NUM_LEDS + 35) - i;

    for (uint16_t i = 0; i < (stripData[5].numLeds-35); i++)
        stripMapping[5][35+i] = i;
}

void setupOTA()
{
    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);

    // Hostname defaults to esp8266-[ChipID]
    ArduinoOTA.setHostname(deviceName);

    // No authentication by default
    // ArduinoOTA.setPassword("admin");

    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    ArduinoOTA.onStart([]()
    {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
        {
            type = "sketch";
        }
        else
        {  // U_FS
            type = "filesystem";
        }

        // NOTE: if updating FS this would be the place to unmount FS using FS.end()
        Log.printf("Start updating %s\n", type);
        otaUpdating = true;
    });
/* 
    ArduinoOTA.onEnd([]()
    {
        Log.println("\nEnd");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
    {
        Log.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
*/
    ArduinoOTA.onError([](ota_error_t error)
    {
        otaUpdating = false;
        Log.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
        {
            Log.println("Auth Failed");
        }
        else if (error == OTA_BEGIN_ERROR)
        {
            Log.println("Begin Failed");
        }
        else if (error == OTA_CONNECT_ERROR)
        {
            Log.println("Connect Failed");
        }
        else if (error == OTA_RECEIVE_ERROR)
        {
            Log.println("Receive Failed");
        }
        else if (error == OTA_END_ERROR)
        {
            Log.println("End Failed");
        }
    });

    ArduinoOTA.begin();
}

void setupMQTT()
{
    /*
    mqttPowerButton.addConfigVar("device", deviceConfig);
    mqttPowerState.addConfigVar("device", deviceConfig);
    mqttParentalMode.addConfigVar("device", deviceConfig);
    */

    mqttClient.setBufferSize(4096);
    mqttClient.setServer(mqtt_server, 1883);
    mqttClient.setCallback(mqttCallback);
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
        Log.println(buffer);
    }

    if (strlen(topic) == 38 && strncmp(topic, "home/study/light/front-shelf/speed/set", 38) == 0)
    {
        lightSpeed = atoi( p );
        mqttClient.publish("home/study/light/front-shelf/speed/state", p, true);
    }
    else
    {
        uint8_t topicLen = strlen(topic);

        char command[11];
        memcpy(command, &topic[31], topicLen-35);
        command[topicLen-35] = '\0';

        int8_t strip = -1;
        if (strlen(topic) > 29 && strncmp(topic, "home/study/light/front-shelf/", 29) == 0) 
        {
            strip = topic[29] - '0' - 1;
        }

        if (strip < 0)
            return;

        if (strcmp(command, "switch") == 0)
        {
            bool state = strcmp(p, "ON") == 0;

            if (stripData[strip].enabled == state)
                return;
            else
                stripData[strip].enabled = state;
        
            bool psuEnabledCheck = false;
            for (uint8_t i = 0; i < logicalStrips; i++) {
                if (stripData[i].enabled)
                    psuEnabledCheck = true;
            }
            psuShouldBeEnabled = psuEnabledCheck;
        }
        else if (strcmp(command, "brightness") == 0)
        {
            uint8_t brightness;
            sscanf(p, "%d", &brightness);
            stripData[strip].targetBrightness = brightness;
        }
        else if (strcmp(command, "rgb") == 0)
        {
            char * token = strtok(p, ",");

            uint8_t c[3];
            for (int i = 0; i < 3; i++) {
                c[i] = atoi(token);
                token = strtok(NULL, ",");
            }
            stripData[strip].color = (c[0] << 16) | (c[1] << 8) | c[2];
        }
        else if (strcmp(command, "effect") == 0)
        {
            if (strcmp(p, "Rainbow") == 0)
                stripData[strip].effect = LIGHT_EFFECT_RAINBOW;
            else if (strcmp(p, "Henry") == 0)
            {
                stripData[strip].effect = LIGHT_EFFECT_HENRY;
                HenryColor = 0;
                HenryColorOrBlack = false;
                HenryLedNumber = 0;
            }
            else
                stripData[strip].effect = LIGHT_EFFECT_SOLID;
        }

        char bufferTopic[50];
        char bufferPayload[15];
        snprintf(bufferTopic, sizeof(bufferTopic), "home/study/light/front-shelf/%d/switch", strip+1);
        mqttClient.publish(bufferTopic, stripData[strip].enabled ? "ON" : "OFF");
        snprintf(bufferTopic, sizeof(bufferTopic), "home/study/light/front-shelf/%d/brightness", strip+1);
        snprintf(bufferPayload, sizeof(bufferPayload), "%d", stripData[strip].targetBrightness);
        mqttClient.publish(bufferTopic, bufferPayload);
        snprintf(bufferTopic, sizeof(bufferTopic), "home/study/light/front-shelf/%d/rgb", strip+1);
        snprintf(bufferPayload, sizeof(bufferPayload), "%d,%d,%d", (stripData[strip].color >> 16) & 0xff, (stripData[strip].color >> 8) & 0xff, stripData[strip].color & 0xff);
        mqttClient.publish(bufferTopic, bufferPayload);
        snprintf(bufferTopic, sizeof(bufferTopic), "home/study/light/front-shelf/%d/effect", strip+1);
        mqttClient.publish(bufferTopic, stripData[strip].effect == LIGHT_EFFECT_RAINBOW ? "Rainbow" : stripData[strip].effect == LIGHT_EFFECT_HENRY ? "Henry" : "Solid");
    }
}

void mqttConnect()
{
    Log.println("Connecting to MQTT");
    // Attempt to connect
    if (mqttClient.connect(deviceName, mqtt_username, mqtt_password))
    {
        Log.println("Connected to MQTT");
        nextMqttConnectAttempt = 0;

        mqttClient.subscribe("home/study/light/front-shelf/+/+/set");
        mqttClient.subscribe("home/study/light/front-shelf/speed/set");
        
        char buffer[40];
        for (int i = 0; i < logicalStrips; i++)
        {
            snprintf(buffer, sizeof(buffer), "home/study/light/front-shelf/%d/switch", i+1);
            mqttClient.publish(buffer, stripData[i].enabled ? "ON" : "OFF");
        }
        std::string s = std::to_string(lightSpeed);
        mqttClient.publish("home/study/light/front-shelf/speed/state", s.c_str());
    }
    else
    {
        Log.println("Failed to connect to MQTT");
        nextMqttConnectAttempt = millis() + mqttReconnectInterval;
    }
}

void manageMQTT()
{
    if (mqttClient.connected())
    {
        mqttClient.loop();

        if (millis() > nextMetricsUpdate)
        {
            sendTelegrafMetrics();
            nextMetricsUpdate = millis() + 30000;
        }

    }
    else if (millis() > nextMqttConnectAttempt)
    {
        mqttConnect();
    }
}

void sendTelegrafMetrics()
{
    uint32_t uptime = rp2040.getCycleCount64() / F_CPU; //133000000L;

    char buffer[150];

    snprintf(buffer, sizeof(buffer),
        "status,device=%s uptime=%ld,memUsed=%ld,memTotal=%ld,firmware=\"%s\"",
        deviceName,
        uptime,
        rp2040.getUsedHeap(),
        rp2040.getTotalHeap(),
        ARDUINO_PICO_VERSION_STR);
    mqttClient.publish("telegraf/particle", buffer);
}

void manageOnboardLED()
{
    if (startupComplete)
        return;

    // TURN OFF ONBOARD LED ONCE UPTIME IS GREATER THEN 5 SECONDS
    if (millis() > 5000)
    {
        digitalWrite(ONBOARD_LED_PIN, LOW);
        startupComplete = true;
    }
}

void manageWiFi()
{
    // if WiFi is down, try reconnecting
    if ((WiFi.status() != WL_CONNECTED) && (millis() - wifiReconnectPreviousMillis >= wifiReconnectInterval))
    {
        if (wifiReconnectCount >= 10)
        {
            rp2040.restart();
        }
        
        wifiReconnectCount++;

        connectToNetwork();

        if (WiFi.status() == WL_CONNECTED)
        {
            wifiReconnectCount = 0;
            wifiReconnectPreviousMillis = 0;
            Log.println("Reconnected to WiFi");
        }
        else
        {
          wifiReconnectPreviousMillis = millis();
        }
    }
}

void managePSU()
{
    if (psuShouldBeEnabled && !psuReady)
    {
        if (!psuEnabled)
        {
            digitalWrite(relayPin, HIGH);
            psuEnabled = true;
            psuReady = false;
            psuActionableTime = millis() + psuStartupBuffer;
        }
        else if (millis() > psuActionableTime)
        {
            psuReady = true;
            psuActionableTime = 0;
        }
    }

    if (!psuShouldBeEnabled && psuEnabled)
    {
        if (psuActionableTime == 0)
        {
            psuActionableTime = millis() + psuShutdownBuffer;
        }
        else if (millis() > psuActionableTime)
        {
            psuActionableTime = 0;
            digitalWrite(relayPin, LOW);
            psuReady = false;
            psuEnabled = false;
        }
    }

    if (psuShouldBeEnabled && psuReady && psuActionableTime != 0)
        psuActionableTime = 0;
}

void fillRainbow(uint16_t first_hue, uint8_t strip)
{
    uint8_t saturation = 255;
    uint8_t brightness = 255;

    for (uint16_t i = 0; i < stripData[strip].numLeds; i++)
    {
        uint16_t hue = first_hue + (i * 65536) / stripData[strip].numLeds;
        uint32_t color = leds.ColorHSV(hue, saturation, brightness);
        color = leds.gamma32(color);
        leds.setPixelColor(stripMapping[strip][i], color);
    }
}

void fadeyShelfy(uint8_t strip)
{
    uint32_t targetColor = stripData[strip].color;
    uint8_t targetR = (targetColor >> 16) & 0xff; // red
    uint8_t targetG = (targetColor >> 8) & 0xff; // green
    uint8_t targetB = targetColor & 0xff; // blue

    for (int j = 0; j < stripData[strip].numLeds; j++)
    {
        uint32_t currentColor = leds.getPixelColor(stripMapping[strip][j]);
        if (currentColor != targetColor)
        {
            uint8_t currentR = (currentColor >> 16) & 0xff; // red
            uint8_t currentG = (currentColor >> 8) & 0xff; // green
            uint8_t currentB = currentColor & 0xff; // blue

            targetR = currentR >= targetR+5 ? currentR-5 : (currentR <= targetR-5 ? currentR+5 : targetR);
            targetG = currentG >= targetG+5 ? currentG-5 : (currentG <= targetG-5 ? currentG+5 : targetG);
            targetB = currentB >= targetB+5 ? currentB-5 : (currentB <= targetB-5 ? currentB+5 : targetB);

            targetColor = leds.Color(targetR, targetG, targetB);

            leds.setPixelColor(stripMapping[strip][j], targetColor);
        }
    }
}

void manageLeds()
{
    if (!psuReady || millis() < nextLedUpdate)
        return;

    if (showFPS) {
        fpsCount++;

        if (millis() > nextFPSCount) {
            nextFPSCount = millis() + 1000;
            char buffer[10];
            snprintf(buffer, 10, "%d", fpsCount);
            mqttClient.publish("log/fps", buffer);
            fpsCount = 0;
        }
    }

    indexHue += lightSpeed;


    for (uint8_t strip = 0; strip < logicalStrips; strip++)
    {
        if (!stripData[strip].enabled && !stripData[strip].active && stripData[strip].brightness == 0)
            continue;

        if (stripData[strip].enabled && !stripData[strip].active)
        {
            stripData[strip].active = true;
            stripData[strip].brightness = 0;

            for (uint8_t i = 0; i < stripData[strip].numLeds; i++)
                leds.setPixelColor(stripMapping[strip][i], 0);
        }

        if (stripData[strip].enabled && stripData[strip].brightness != stripData[strip].targetBrightness)
        {
            if (strip == 0)
            {
                /*
                if (stripData[strip].brightness <= (stripData[strip].targetBrightness-5))
                    stripData[strip].brightness += 5;
                else if (stripData[strip].targetBrightness <= (stripData[strip].brightness-5))
                    stripData[strip].brightness -= 5;
                else
                    stripData[strip].brightness = stripData[strip].targetBrightness;
                */

                if (stripData[strip].brightness < stripData[strip].targetBrightness)
                    stripData[strip].brightness++;
                else if (stripData[strip].targetBrightness < stripData[strip].brightness)
                    stripData[strip].brightness--;
                
                leds.setBrightness(stripData[strip].brightness);
            }
        }
        else if (!stripData[strip].enabled && stripData[strip].brightness != 0)
        {
            if (strip == 0)
            {
                //if (stripData[strip].brightness >= 5)
                //    stripData[strip].brightness -= 5;
                //else
                //    stripData[strip].brightness = 0;
                stripData[strip].brightness--;
                leds.setBrightness(stripData[strip].brightness);
            }
        }
        else if (!stripData[strip].enabled && stripData[strip].brightness == 0 && stripData[strip].active)
        {
            stripData[strip].active = false;
        }

        if (stripData[strip].effect == LIGHT_EFFECT_RAINBOW)
        {
            fillRainbow(indexHue, strip);
        }
        else if (stripData[strip].effect == LIGHT_EFFECT_HENRY)
        {
            uint8_t loopCount = lightSpeed / 10;
            
            loopCount = loopCount < 1 ? 1: loopCount;

            if (HenryLedNumber < stripData[strip].numLeds)
            {
                for (uint8_t i = 0; i < loopCount; i++)
                {
                    leds.setPixelColor(stripMapping[strip][HenryLedNumber+i], HenryColorOrBlack ? HenryColors[HenryColor] : 0);    
                }
                
            }

            if (strip == 5)
            {
                if (HenryLedNumber+loopCount >= 96)
                {
                    HenryLedNumber = 0;
                    HenryColorOrBlack = !HenryColorOrBlack;
                    if (!HenryColorOrBlack)
                        HenryColor = HenryColor == 4 ? 0 : HenryColor + 1;
                }
                else
                {
                    HenryLedNumber += loopCount;
                }
            }
        }
        else if (stripData[strip].effect == LIGHT_EFFECT_SOLID)
        {
            fadeyShelfy(strip);
        }
    }

    leds.show();
    nextLedUpdate = millis() + (1000 / targetFPS);
}

void setup()
{
    pinMode(ONBOARD_LED_PIN, OUTPUT);
    pinMode(relayPin, OUTPUT);
    digitalWrite(relayPin, LOW);
    digitalWrite(ONBOARD_LED_PIN, HIGH);

    Log.setup();
    
    connectToNetwork();

    buildLedMapping();

    if (!leds.begin()) {
        // Blink the onboard LED if that happens.
        pinMode(LED_BUILTIN, OUTPUT);
        for (;;) digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
    }

    setupOTA();

    setupMQTT();
}

void loop()
{
    ArduinoOTA.handle();

    if (otaUpdating)
        return;

    manageWiFi();

    manageMQTT();

    manageOnboardLED();

    managePSU();

    manageLeds();
}
