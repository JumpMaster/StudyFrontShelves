#ifndef STUDY_FRONT_SHELVES_PICO_H
#define STUDY_FRONT_SHELVES_PICO_H

#include <string>
/*
#include <WiFi.h>
#include <WiFiUdp.h>

#include "Logging.h"
#include <ArduinoOTA.h>
#include <PubSubClient.h>
*/
#include <Adafruit_NeoPXL8.h>
#include "secrets.h"

const bool isDebug = false;

bool startupComplete = false;
bool otaUpdating = false;

#define NUM_LEDS    96
#define COLOR_ORDER NEO_RGB
/*
const uint8_t ONBOARD_LED_PIN = 13;
uint32_t nextOnboardLedUpdate = 0;
bool onboardLedState = true;
*/
const uint8_t relayPin = 17;
int8_t ledPins[8] = { 12, 11, 10, 9, 6, 5, 4, -1 };
Adafruit_NeoPXL8 leds(NUM_LEDS, ledPins, COLOR_ORDER);

typedef enum
{
    LIGHT_EFFECT_SOLID = 0,
    LIGHT_EFFECT_RAINBOW = 1,
    LIGHT_EFFECT_HENRY = 2,
} LightEffect;

struct StripData
{
    const uint8_t numLeds;
    bool enabled;
    bool active;
    uint8_t effect;
    uint8_t targetBrightness;
    uint8_t brightness;
    uint32_t color;
};

const uint8_t logicalStrips = 6;
uint16_t stripMapping[logicalStrips][NUM_LEDS];

StripData stripData[logicalStrips] =
{
    { 95, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215},
    { 95, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215},
    { 96, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215},
    { 82, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215},
    { 82, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215},
    { 75, false, false, LIGHT_EFFECT_SOLID, 255, 0, 16777215}, // 35 + 40 = 75
};
/*
WiFiClient rpiClient;
unsigned long wifiReconnectPreviousMillis = 0;
unsigned long wifiReconnectInterval = 30000;
uint8_t wifiReconnectCount = 0;

PubSubClient mqttClient(rpiClient);
uint32_t nextMqttConnectAttempt = 0;
const uint32_t mqttReconnectInterval = 10000;
uint32_t nextMetricsUpdate = 0;
*/
uint16_t indexHue = 0;
uint8_t lightSpeed = 10;

uint32_t nextLedUpdate = 0;
uint16_t targetFPS = 120;
bool showFPS = false;
uint32_t nextFPSCount;
uint32_t fpsCount;

bool psuShouldBeEnabled = false;
bool psuEnabled = false;
bool psuReady = false;
uint32_t psuActionableTime = 0;
const uint16_t psuShutdownBuffer = 10000;
const uint16_t psuStartupBuffer = 100;

// RED, YELLOW, GREEN, BLUE, PURPLE
const uint32_t HenryColors[] = {16711680, 16776960, 65280, 255, 16711935};
uint8_t HenryColor = 0;
bool HenryColorOrBlack = false;
uint8_t HenryLedNumber = 0;
/*
const uint32_t NEOPIXEL_BLACK = 0;
const uint32_t NEOPIXEL_RED =       Adafruit_NeoPixel::Color(255, 0,   0);
const uint32_t NEOPIXEL_ORANGE =    Adafruit_NeoPixel::Color(255, 165, 0);
const uint32_t NEOPIXEL_YELLOW =    Adafruit_NeoPixel::Color(255, 255, 0);
const uint32_t NEOPIXEL_MAGENTA =   Adafruit_NeoPixel::Color(255, 0,   255);
const uint32_t NEOPIXEL_GREEN =     Adafruit_NeoPixel::Color(0,   255, 0);
const uint32_t NEOPIXEL_CYAN =      Adafruit_NeoPixel::Color(0,   255, 255);
const uint32_t NEOPIXEL_BLUE =      Adafruit_NeoPixel::Color(0,   0,   255);
const uint32_t NEOPIXEL_WHITE =     Adafruit_NeoPixel::Color(255, 255, 255);

#ifdef DIAGNOSTIC_PIXEL_PIN
Adafruit_NeoPixel diagnosticPixel(1, DIAGNOSTIC_PIXEL_PIN, NEO_GRB + NEO_KHZ800);

uint8_t diagnosticPixelMaxBrightness = 64;
uint8_t diagnosticPixelBrightness = diagnosticPixelMaxBrightness;
bool diagnosticPixelBrightnessDirection = 0;
uint32_t diagnosticPixelColor1 = 0xFF0000;
volatile uint32_t diagnosticPixelColor2 = 0x000000;
uint32_t currentDiagnosticPixelColor = diagnosticPixelColor1;
uint32_t nextDiagnosticPixelUpdate = 0;

void setupDiagnosticPixel()
{
    pinMode(NEOPIXEL_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_POWER, HIGH);

    diagnosticPixel.begin();
    diagnosticPixel.setPixelColor(0, NEOPIXEL_RED);
    diagnosticPixel.setBrightness(diagnosticPixelMaxBrightness);
    diagnosticPixel.show();
}

void manageDiagnosticPixel()
{
    if (millis() < nextDiagnosticPixelUpdate)
        return;

    if (mqttClient.connected())
        diagnosticPixelColor1 = NEOPIXEL_GREEN;
    else if (WiFi.status() == WL_CONNECTED)
        diagnosticPixelColor1 = NEOPIXEL_BLUE;
    else    
        diagnosticPixelColor1 = NEOPIXEL_RED;
    
    if (diagnosticPixelBrightness <= 0)
    {
        diagnosticPixelBrightnessDirection = 1;
        if (diagnosticPixelColor2 != NEOPIXEL_BLACK && currentDiagnosticPixelColor == diagnosticPixelColor1)
            currentDiagnosticPixelColor = diagnosticPixelColor2;
        else
            currentDiagnosticPixelColor = diagnosticPixelColor1;
    }
    else if (diagnosticPixelBrightness >= diagnosticPixelMaxBrightness)
    {
        diagnosticPixelBrightnessDirection = 0;
    }
    
    diagnosticPixelBrightness = diagnosticPixelBrightnessDirection ? diagnosticPixelBrightness+1 : diagnosticPixelBrightness-1;
    diagnosticPixel.setPixelColor(0, currentDiagnosticPixelColor);
    diagnosticPixel.setBrightness(diagnosticPixelBrightness);
    diagnosticPixel.show();

    nextDiagnosticPixelUpdate = millis() + 33; // 33 = 30 FPS
}
#endif
*/
#endif