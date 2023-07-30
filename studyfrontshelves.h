#ifndef STUDY_FRONT_SHELVES_PICO_H
#define STUDY_FRONT_SHELVES_PICO_H

#include <string>
#include <Adafruit_NeoPXL8.h>
#include "secrets.h"

const bool isDebug = false;

bool startupComplete = false;
bool otaUpdating = false;

#define NUM_LEDS    96
#define COLOR_ORDER NEO_RGB

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

#endif