#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

enum class LightEffect : uint8_t {
    Static = 0,
    Rainbow = 1,
    Fire = 2,
    Waves = 3,
    Twinkle = 4,
    Cycle = 5
};

struct LightConfig {
    bool enabled;
    uint8_t brightness;
    uint32_t color;
    LightEffect effect;
    bool alarmEnabled;
    uint16_t alarmMinutes[7]; // Sunday first, matching tm_wday.
    String mqttToken;
};

extern LightConfig lightConfig;

void initLight();
void updateLight();
void setLightEnabled(bool enabled);
void startSunrise(uint8_t targetBrightness);
void testSunrise(uint8_t targetBrightness, uint16_t durationMs = 15000);
void serializeLightConfig(JsonObject object);
bool loadLightConfig(JsonVariant object);
bool saveLightConfig(const LightConfig& config);
