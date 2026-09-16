#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include "ConfigManager.h"
#include "WebInterface.h"
#include "TimeManager.h"
#include "LightManager.h"
#include "MqttManager.h"

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[System] Старт системы LightNode...");

    // 1. Инициализация файловой системы
    if (!LittleFS.begin()) {
        Serial.println("[System] Критическая ошибка LittleFS!");
        return;
    }

    // 2. Инициализация и чтение конфигурации из памяти
    initConfig();
    initLight();
    initMqtt();

    // 3. Умная логика старта Wi-Fi на основе сохраненного режима
    if (currentConfig.mode == "STA") {
        Serial.printf("[WiFi] Запуск в режиме клиента. Подключение к: %s\n", currentConfig.ssid.c_str());
        WiFi.mode(WIFI_STA);
        WiFi.begin(currentConfig.ssid.c_str(), currentConfig.password.c_str());

        // Ждем подключения максимум 15 секунд
        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("\n[WiFi] Успешно подключено! Рабочий IP: ");
            Serial.println(WiFi.localIP());
            initTime();
        } else {
            // Аварийный режим, если домашний роутер выключен или неверный пароль
            Serial.println("\n[WiFi] Не удалось подключиться. Аварийный запуск Точки Доступа...");
            WiFi.mode(WIFI_AP);
            WiFi.softAP("LightNode_RECOVERY", "12345678");
            Serial.print("[WiFi] Аварийный IP адрес: ");
            Serial.println(WiFi.softAPIP());
        }
    } 
    else { // Если сохранен режим "AP"
        Serial.printf("[WiFi] Запуск в режиме Точки Доступа: %s\n", currentConfig.ssid.c_str());
        WiFi.mode(WIFI_AP);
        WiFi.softAP(currentConfig.ssid.c_str(), currentConfig.password.c_str());
        Serial.print("[WiFi] IP адрес панели: ");
        Serial.println(WiFi.softAPIP());
    }
    // 4. Инициализация сервера
    initWebServer();
}

void loop() {
    handleWebServer();
    updateLight();
    updateMqtt();
}
