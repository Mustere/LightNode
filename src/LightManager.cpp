#include "LightManager.h"

#define FASTLED_INTERNAL
#include <FastLED.h>
#include <LittleFS.h>
#include <time.h>

#include "ConfigManager.h"
#include "TimeManager.h"

namespace {
constexpr uint8_t LED_PIN = D5;
constexpr uint16_t LED_COUNT = 120;
constexpr uint32_t LIGHT_UPDATE_INTERVAL = 50;
constexpr uint32_t MUSIC_UPDATE_INTERVAL = 20;
constexpr uint32_t SUNRISE_STEP_INTERVAL = 60000;
constexpr uint8_t MUSIC_BASS_THRESHOLD = 160;
constexpr uint8_t MUSIC_BRIGHTNESS_LIMIT = 96;
constexpr uint16_t MUSIC_FLASH_LENGTH = 12;
constexpr uint32_t MUSIC_FLASH_COOLDOWN = 220;
constexpr uint32_t MUSIC_FLASH_DURATION = 110;
constexpr uint32_t MUSIC_FRAME_TIMEOUT = 250;

CRGB leds[LED_COUNT];
uint8_t hue = 0;
uint32_t lastFrame = 0;
bool sunriseActive = false;
uint8_t sunriseTarget = 0;
uint8_t sunriseBrightness = 0;
uint32_t lastSunriseStep = 0;
bool stripWasOff = false;
int lastAlarmDay = -1;
bool sunriseTestActive = false;
uint8_t sunriseTestTarget = 0;
uint32_t sunriseTestStarted = 0;
uint16_t sunriseTestDuration = 0;
uint32_t lastMusicFlash = 0;
uint32_t lastMusicFrameAt = 0;
uint32_t musicFlashStarted = 0;
uint16_t musicFlashStart = 0;
CRGB musicFlashColor;
bool musicFlashActive = false;

CRGB configuredColor() {
    return CRGB((lightConfig.color >> 16) & 0xff,
                (lightConfig.color >> 8) & 0xff,
                lightConfig.color & 0xff);
}

void showStatic(uint8_t brightness) {
    FastLED.setBrightness(brightness);
    fill_solid(leds, LED_COUNT, configuredColor());
    FastLED.show();
}

void renderEffect() {
    FastLED.setBrightness(lightConfig.brightness);
    switch (lightConfig.effect) {
        case LightEffect::Rainbow:
            fill_rainbow(leds, LED_COUNT, hue++, 7);
            break;
        case LightEffect::Pulse: {
            // Мягкое дыхание настроенного цвета — WS2812 без этого выглядит слишком кричаще,
            // поэтому пульсируем яркость в комфортном диапазоне.
            const uint8_t breath = beatsin8(12, 60, 220);
            fill_solid(leds, LED_COUNT, configuredColor());
            for (uint16_t i = 0; i < LED_COUNT; ++i) {
                leds[i].nscale8(breath);
            }
            break;
        }
        case LightEffect::Waves:
            for (uint16_t i = 0; i < LED_COUNT; ++i) {
                leds[i] = CHSV(sin8(i * 8 + hue) / 2 + 128, 255, 255);
            }
            hue += 2;
            break;
        case LightEffect::Twinkle:
            fadeToBlackBy(leds, LED_COUNT, 10);
            if (random8() < 50) {
                leds[random16(LED_COUNT)] += CHSV(random8(), 200, 255);
            }
            break;
        case LightEffect::Cycle:
            fill_solid(leds, LED_COUNT, CHSV(hue++, 255, 255));
            break;
        case LightEffect::Music:
        {
            const uint32_t now = millis();
            const bool frameFresh = now - lastMusicFrameAt <= MUSIC_FRAME_TIMEOUT;
            const CRGB spectrum = frameFresh ? configuredColor() : CRGB::Black;
            FastLED.setBrightness(min<uint8_t>(lightConfig.brightness, MUSIC_BRIGHTNESS_LIMIT));
            fill_solid(leds, LED_COUNT, CRGB(8, 3, 0));

            if (!frameFresh) {
                musicFlashActive = false;
            } else if (spectrum.r >= MUSIC_BASS_THRESHOLD &&
                       now - lastMusicFlash >= MUSIC_FLASH_COOLDOWN) {
                lastMusicFlash = now;
                musicFlashStarted = now;
                musicFlashStart = random16(LED_COUNT - MUSIC_FLASH_LENGTH + 1);
                musicFlashColor = spectrum;
                musicFlashActive = true;
            }

            if (musicFlashActive) {
                const uint32_t elapsed = now - musicFlashStarted;
                if (elapsed >= MUSIC_FLASH_DURATION) {
                    musicFlashActive = false;
                } else {
                    const uint8_t level = 255 - static_cast<uint8_t>(
                        (elapsed * 255) / MUSIC_FLASH_DURATION);
                    CRGB flash = musicFlashColor;
                    flash.nscale8(level);
                    for (uint16_t i = musicFlashStart; i < musicFlashStart + MUSIC_FLASH_LENGTH; ++i) {
                        leds[i] += flash;
                    }
                }
            }
            break;
        }
        case LightEffect::Static:
        default:
            fill_solid(leds, LED_COUNT, configuredColor());
            break;
    }
    FastLED.show();
}

void stopStrip() {
    if (!stripWasOff) {
        FastLED.clear(true);
        FastLED.show();
        stripWasOff = true;
    }
}
} // namespace

LightConfig lightConfig = {
    true,
    192,
    0xFFA000,
    LightEffect::Static,
    false,
    {360, 480, 480, 480, 480, 600, 600},
    {0xFF7715, 0x001525, 0x00331A, 0x1A0033, 0x2A1A4A, 0x1A332A},
    ""
};

void initLight() {
    FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
    FastLED.setBrightness(lightConfig.brightness);
    stripWasOff = false;
    showStatic(lightConfig.enabled ? lightConfig.brightness : 0);
}

void setLightEnabled(bool enabled) {
    lightConfig.enabled = enabled;
    if (!enabled) {
        sunriseActive = false;
        sunriseTestActive = false;
        stopStrip();
        return;
    }

    stripWasOff = false;
    showStatic(lightConfig.brightness);
}

void noteMusicFrameReceived() {
    lastMusicFrameAt = millis();
}

void updateLight() {
    const uint32_t now = millis();
    if (sunriseTestActive) {
        const uint32_t elapsed = now - sunriseTestStarted;
        if (elapsed >= sunriseTestDuration) {
            sunriseTestActive = false;
            showStatic(sunriseTestTarget);
        } else {
            const uint8_t brightness = static_cast<uint8_t>(
                (static_cast<uint32_t>(sunriseTestTarget) * elapsed) / sunriseTestDuration);
            showStatic(brightness);
        }
        return;
    }

    if (!sunriseActive && lightConfig.alarmEnabled && isTimeValid()) {
        time_t current = time(nullptr);
        struct tm* timeInfo = localtime(&current);
        const int alarm = lightConfig.alarmMinutes[timeInfo->tm_wday];
        const int currentMinutes = timeInfo->tm_hour * 60 + timeInfo->tm_min;
        const int sunriseMinutes = (alarm + 24 * 60 - 30) % (24 * 60);
        if (currentMinutes == sunriseMinutes && lastAlarmDay != timeInfo->tm_yday) {
            lastAlarmDay = timeInfo->tm_yday;
            startSunrise(lightConfig.brightness);
        }
    }
    if (sunriseActive) {
        if (now - lastSunriseStep >= SUNRISE_STEP_INTERVAL) {
            lastSunriseStep = now;
            sunriseBrightness = min<uint8_t>(
                sunriseTarget, sunriseBrightness + max<uint8_t>(1, sunriseTarget / 30));
            showStatic(sunriseBrightness);
            if (sunriseBrightness >= sunriseTarget) {
                sunriseActive = false;
                lightConfig.enabled = true;
            }
        }
        return;
    }

    if (!lightConfig.enabled) {
        stopStrip();
        return;
    }

    stripWasOff = false;
    const uint32_t updateInterval = lightConfig.effect == LightEffect::Music
        ? MUSIC_UPDATE_INTERVAL : LIGHT_UPDATE_INTERVAL;
    if (now - lastFrame < updateInterval) return;
    lastFrame = now;
    renderEffect();
}

void startSunrise(uint8_t targetBrightness) {
    setLightEnabled(true);
    sunriseActive = true;
    sunriseTarget = targetBrightness;
    sunriseBrightness = 0;
    lastSunriseStep = millis();
    showStatic(0);
}

void testSunrise(uint8_t targetBrightness, uint16_t durationMs) {
    setLightEnabled(true);
    sunriseTestActive = true;
    sunriseTestTarget = targetBrightness;
    sunriseTestStarted = millis();
    sunriseTestDuration = max<uint16_t>(1, durationMs);
}

void serializeLightConfig(JsonObject object) {
    object["enabled"] = lightConfig.enabled;
    object["brightness"] = lightConfig.brightness;
    object["color"] = lightConfig.color;
    object["effect"] = static_cast<uint8_t>(lightConfig.effect);
    object["alarm_enabled"] = lightConfig.alarmEnabled;
    object["mqtt_token"] = lightConfig.mqttToken;
    JsonArray alarms = object["alarm_minutes"].to<JsonArray>();
    for (uint8_t i = 0; i < 7; ++i) alarms.add(lightConfig.alarmMinutes[i]);
    JsonArray favorites = object["favorite_colors"].to<JsonArray>();
    for (uint8_t i = 0; i < 6; ++i) favorites.add(lightConfig.favoriteColors[i]);
}

bool loadLightConfig(JsonVariant object) {
    if (!object.is<JsonObject>()) return false;
    lightConfig.enabled = object["enabled"] | lightConfig.enabled;
    lightConfig.brightness = object["brightness"] | lightConfig.brightness;
    lightConfig.color = object["color"] | lightConfig.color;
    const uint8_t effect = object["effect"] | static_cast<uint8_t>(LightEffect::Static);
    lightConfig.effect = effect <= static_cast<uint8_t>(LightEffect::Music)
        ? static_cast<LightEffect>(effect) : LightEffect::Static;
    lightConfig.alarmEnabled = object["alarm_enabled"] | lightConfig.alarmEnabled;
    lightConfig.mqttToken = object["mqtt_token"] | lightConfig.mqttToken;
    JsonArray alarms = object["alarm_minutes"].as<JsonArray>();
    for (uint8_t i = 0; i < 7 && i < alarms.size(); ++i) {
        lightConfig.alarmMinutes[i] = alarms[i] | lightConfig.alarmMinutes[i];
    }
    JsonArray favorites = object["favorite_colors"].as<JsonArray>();
    for (uint8_t i = 0; i < 6 && i < favorites.size(); ++i) {
        lightConfig.favoriteColors[i] = favorites[i] | 0;
    }
    return true;
}

bool saveLightConfig(const LightConfig& config) {
    lightConfig = config;
    return saveConfig();
}
