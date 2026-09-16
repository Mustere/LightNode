#include "ConfigManager.h"
#include "LightManager.h"

// Выделяем память под глобальный объект настроек
WifiConfig currentConfig;

const char* CONFIG_FILE = "/config.json";

bool initConfig() {
    // Проверяем наличие файла конфигурации
    if (!LittleFS.exists(CONFIG_FILE)) {
        Serial.println("[Config] Файл настроек отсутствует. Создаю дефолтный (AP)...");
        // Задаем настройки по умолчанию для первого запуска
        return saveWiFiConfig("AP", "LightNode_AP", "12345678");
    }
    return loadConfig();
}

bool loadConfig() {
    currentConfig.mode = "AP";
    currentConfig.ssid = "LightNode_AP";
    currentConfig.password = "12345678";

    File configFile = LittleFS.open(CONFIG_FILE, "r");
    if (!configFile) {
        Serial.println("[Config] Файл не найден. Используем дефолт.");
        return false; // Теперь вернет false, но config уже заполнен безопасными данными!
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        Serial.println("[Config] Ошибка парсинга JSON. Используем дефолт.");
        return false;
    }

    currentConfig.mode = doc["wifi_mode"] | "AP";
    currentConfig.ssid = doc["ssid"] | "LightNode_AP";
    currentConfig.password = doc["password"] | "12345678";
    loadLightConfig(doc["light"]);

    Serial.println("[Config] Конфигурация успешно загружена из Flash");
    return true;
}

bool saveConfig() {
    JsonDocument doc;
    if (LittleFS.exists(CONFIG_FILE)) {
        File configFile = LittleFS.open(CONFIG_FILE, "r");
        if (configFile) {
            deserializeJson(doc, configFile);
            configFile.close();
        }
    }
    doc["wifi_mode"] = currentConfig.mode;
    doc["ssid"] = currentConfig.ssid;
    doc["password"] = currentConfig.password;
    serializeLightConfig(doc["light"].to<JsonObject>());

    File configFile = LittleFS.open(CONFIG_FILE, "w");
    if (!configFile || serializeJson(doc, configFile) == 0) {
        if (configFile) configFile.close();
        Serial.println("[Config] Ошибка записи настроек");
        return false;
    }
    configFile.close();
    return true;
}


bool saveWiFiConfig(const String& mode, const String& ssid, const String& password) {
    JsonDocument doc;
    
    // Preserve the other persisted settings when updating Wi-Fi fields.
    if (LittleFS.exists(CONFIG_FILE)) {
        File configFile = LittleFS.open(CONFIG_FILE, "r");
        if (configFile) {
            deserializeJson(doc, configFile);
            configFile.close();
        }
    }

    // Обновляем только поля Wi-Fi
    doc["wifi_mode"] = mode;
    doc["ssid"] = ssid;
    doc["password"] = password;
    serializeLightConfig(doc["light"].to<JsonObject>());

    // Записываем обновленный JSON обратно во флеш
    File configFile = LittleFS.open(CONFIG_FILE, "w");
    if (!configFile) {
        Serial.println("[Config] Ошибка записи файла конфигурации!");
        return false;
    }

    if (serializeJson(doc, configFile) == 0) {
        Serial.println("[Config] Ошибка сериализации JSON при записи");
        configFile.close();
        return false;
    }

    configFile.close();
    
    // Синхронизируем данные в оперативной памяти
    currentConfig.mode = mode;
    currentConfig.ssid = ssid;
    currentConfig.password = password;

    Serial.println("[Config] Новые настройки Wi-Fi успешно сохранены во Flash");
    return true;
}
