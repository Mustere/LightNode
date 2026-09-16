#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "LightManager.h"

// Структура для удобного хранения настроек Wi-Fi в оперативной памяти
struct WifiConfig {
    String mode;     // "STA" или "AP"
    String ssid;     // Имя сети
    String password; // Пароль
};

// Внешний доступ к текущим настройкам
extern WifiConfig currentConfig;

// Интерфейс модуля конфигурации
bool initConfig();
bool loadConfig();
bool saveWiFiConfig(const String& mode, const String& ssid, const String& password);
bool saveConfig();
