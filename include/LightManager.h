#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

enum class LightEffect : uint8_t {
    Static = 0,
    Rainbow = 1,
    Waves = 2,
    Twinkle = 3,
    Cycle = 4,
    Pulse = 5,
    Music = 6
};

struct LightConfig {
    bool enabled;
    uint8_t brightness;
    uint32_t color;
    LightEffect effect;
    bool alarmEnabled;
    uint16_t alarmMinutes[7]; // Sunday first, matching tm_wday.
    uint32_t favoriteColors[6]; // банк избранных цветов (0 = свободный слот)
    String mqttToken;
};

extern LightConfig lightConfig;

void initLight();
void updateLight();
void setLightEnabled(bool enabled);
void noteMusicFrameReceived();
void startSunrise(uint8_t targetBrightness);
void testSunrise(uint8_t targetBrightness, uint16_t durationMs = 15000);
void serializeLightConfig(JsonObject object);
bool loadLightConfig(JsonVariant object);
bool saveLightConfig(const LightConfig& config);
