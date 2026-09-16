#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// экспорт глобальных объектов для использования в других модулях
extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern AsyncEventSource events;

void initWebServer();
void handleWebServer();
void requestReboot();
