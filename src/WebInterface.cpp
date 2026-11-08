#include "WebInterface.h"

#include <ESP8266WiFi.h>
#include <Updater.h>

#include "ConfigManager.h"
#include "LightManager.h"
#include "TimeManager.h"

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

namespace {
bool shouldReboot = false;
uint32_t rebootTimer = 0;

void sendJson(AsyncWebServerRequest* request, JsonDocument& doc) {
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void sendLightStatus(AsyncWebSocketClient* client) {
    JsonDocument doc;
    doc["type"] = "status";
    doc["enabled"] = lightConfig.enabled;
    doc["brightness"] = lightConfig.brightness;
    doc["color"] = lightConfig.color;
    doc["effect"] = static_cast<uint8_t>(lightConfig.effect);
    String response;
    serializeJson(doc, response);
    client->text(response);
}

bool applyLightJson(JsonVariant source) {
    if (!source.is<JsonObject>()) return false;
    if (source["enabled"].is<bool>()) {
        setLightEnabled(source["enabled"].as<bool>());
    }
    lightConfig.brightness = constrain(source["brightness"] | lightConfig.brightness, 0, 255);
    lightConfig.color = source["color"] | lightConfig.color;
    const uint8_t effect = source["effect"] | static_cast<uint8_t>(lightConfig.effect);
    if (effect > static_cast<uint8_t>(LightEffect::Music)) return false;
    lightConfig.effect = static_cast<LightEffect>(effect);
    lightConfig.alarmEnabled = source["alarm_enabled"] | lightConfig.alarmEnabled;
    lightConfig.mqttToken = source["mqtt_token"] | lightConfig.mqttToken;
    JsonArray alarms = source["alarm_minutes"].as<JsonArray>();
    for (uint8_t i = 0; i < 7 && i < alarms.size(); ++i) {
        const int minutes = alarms[i] | lightConfig.alarmMinutes[i];
        if (minutes < 0 || minutes > 1439) return false;
        lightConfig.alarmMinutes[i] = minutes;
    }
    return saveConfig();
}

bool applyLiveLightJson(JsonVariant source) {
    if (!source.is<JsonObject>()) return false;
    if (source["enabled"].is<bool>()) {
        const bool enabled = source["enabled"].as<bool>();
        if (lightConfig.enabled != enabled) setLightEnabled(enabled);
    }
    lightConfig.brightness = constrain(source["brightness"] | lightConfig.brightness, 0, 255);
    lightConfig.color = source["color"] | lightConfig.color;
    const uint8_t effect = source["effect"] | static_cast<uint8_t>(lightConfig.effect);
    if (effect <= static_cast<uint8_t>(LightEffect::Music)) {
        lightConfig.effect = static_cast<LightEffect>(effect);
        if (lightConfig.effect == LightEffect::Music) noteMusicFrameReceived();
    }
    return true;
}
} // namespace

void requestReboot() {
    shouldReboot = true;
    rebootTimer = millis();
}

void initWebServer() {
    server.addHandler(&ws);
    server.addHandler(&events);
    ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type,
                 void* arg, uint8_t* data, size_t length) {
        if (type == WS_EVT_CONNECT) sendLightStatus(client);
        if (type == WS_EVT_DATA) {
            auto* info = static_cast<AwsFrameInfo*>(arg);
            if (!info || info->opcode != WS_TEXT || !info->final ||
                info->index != 0 || info->len != length) {
                if (!info || info->opcode != WS_BINARY || !info->final ||
                    info->index != 0 || info->len != length || length != 5) return;

                const uint8_t effect = data[4];
                if (effect > static_cast<uint8_t>(LightEffect::Music)) return;
                lightConfig.brightness = data[0];
                lightConfig.color = (static_cast<uint32_t>(data[1]) << 16) |
                    (static_cast<uint32_t>(data[2]) << 8) | data[3];
                lightConfig.effect = static_cast<LightEffect>(effect);
                if (lightConfig.effect == LightEffect::Music) noteMusicFrameReceived();
                return;
            }

            JsonDocument doc;
            if (!deserializeJson(doc, data, length)) applyLiveLightJson(doc.as<JsonVariant>());
        }
    });

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    server.on("/api/get-config", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["wifi_mode"] = currentConfig.mode;
        doc["ssid"] = currentConfig.ssid;
        doc["password"] = currentConfig.password;
        JsonObject light = doc["light"].to<JsonObject>();
        serializeLightConfig(light);
        sendJson(request, doc);
    });

    server.on("/api/get-status", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["enabled"] = lightConfig.enabled;
        doc["brightness"] = lightConfig.brightness;
        doc["color"] = lightConfig.color;
        doc["effect"] = static_cast<uint8_t>(lightConfig.effect);
        sendJson(request, doc);
    });

    server.on("/api/set-light-enabled", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t, size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, length) || !doc["enabled"].is<bool>()) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Неверное состояние подсветки\"}");
            return;
        }

        setLightEnabled(doc["enabled"].as<bool>());
        if (!saveConfig()) {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Ошибка сохранения\"}");
            return;
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/save-light", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t, size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, length) || !applyLightJson(doc)) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Неверные настройки света\"}");
            return;
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/get-favorites", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (uint8_t i = 0; i < 6; ++i) arr.add(lightConfig.favoriteColors[i]);
        sendJson(request, doc);
    });

    server.on("/api/save-favorite", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t, size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, length)) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Неверные данные\"}");
            return;
        }
        const int index = doc["index"] | -1;
        const uint32_t color = doc["color"] | 0;
        if (index < 0 || index > 5) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Неверный слот избранного\"}");
            return;
        }
        lightConfig.favoriteColors[index] = color;
        if (!saveConfig()) {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Ошибка сохранения\"}");
            return;
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/remove-favorite", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t, size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, length)) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Неверные данные\"}");
            return;
        }
        const int index = doc["index"] | -1;
        if (index < 0 || index > 5) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Неверный слот избранного\"}");
            return;
        }
        lightConfig.favoriteColors[index] = 0;
        if (!saveConfig()) {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Ошибка сохранения\"}");
            return;
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/test-sunrise", HTTP_POST, [](AsyncWebServerRequest* request) {
        testSunrise(lightConfig.brightness);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // Живое управление цветом без записи во флеш (для цветомузыки/моста с компа)
    server.on("/api/light-live", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t, size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, length)) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Неверные данные\"}");
            return;
        }
        if (!applyLiveLightJson(doc.as<JsonVariant>())) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Неверные данные\"}");
            return;
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/save-wifi", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t, size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, length)) {
            request->send(400, "application/json", "{\"status\":\"error\"}");
            return;
        }
        const String mode = doc["wifi_mode"] | "AP";
        const String ssid = doc["ssid"] | "";
        const String password = doc["password"] | "";
        lightConfig.mqttToken = doc["mqtt_token"] | lightConfig.mqttToken;
        if ((mode != "AP" && mode != "STA") || ssid.isEmpty() ||
            !saveWiFiConfig(mode, ssid, password)) {
            request->send(400, "application/json", "{\"status\":\"error\"}");
            return;
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        requestReboot();
    });

    server.on("/api/update", HTTP_POST, [](AsyncWebServerRequest* request) {
        const bool failed = Update.hasError();
        request->send(failed ? 500 : 200, "application/json",
                      failed ? "{\"status\":\"error\"}" : "{\"status\":\"ok\"}");
        if (!failed) requestReboot();
    }, [](AsyncWebServerRequest*, String, size_t index, uint8_t* data, size_t length, bool final) {
        if (!index) {
            Update.runAsync(true);
            const uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if (!Update.begin(maxSpace, U_FLASH)) Update.printError(Serial);
        }
        if (!Update.hasError() && Update.write(data, length) != length) Update.printError(Serial);
        if (final && !Update.end(true)) Update.printError(Serial);
    });

    server.on("/heap", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", String(ESP.getFreeHeap()));
    });
    server.begin();
}

void handleWebServer() {
    if (shouldReboot && millis() - rebootTimer >= 2000) ESP.restart();
    static uint32_t lastEvent = 0;
    if (millis() - lastEvent >= 1000) {
        lastEvent = millis();
        const String payload = getCurrentTimeStr() + "|" +
            (WiFi.getMode() == WIFI_STA ? "STA" : "AP") + "|" +
            (lightConfig.enabled ? "ON" : "OFF");
        events.send(payload.c_str(), "status_tick");
    }
}
