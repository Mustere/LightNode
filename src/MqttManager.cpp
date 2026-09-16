#include "MqttManager.h"

#include <ArduinoMqttClient.h>
#include <ESP8266WiFi.h>

#include "LightManager.h"

namespace {
constexpr char MQTT_BROKER[] = "mqtt.flespi.io";
constexpr int MQTT_PORT = 1883;
constexpr char MQTT_TOPIC[] = "inTopic";
constexpr uint32_t MQTT_RECONNECT_INTERVAL = 10000;

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
uint32_t lastReconnectAttempt = 0;
bool mqttConnected = false;

void onMqttMessage(int messageSize) {
    String message;
    while (mqttClient.available() && message.length() < 16) {
        message += static_cast<char>(mqttClient.read());
    }

    if (message == "0") {
        setLightEnabled(false);
    } else if (message == "1") {
        setLightEnabled(true);
    } else {
        const int brightnessPercent = message.toInt();
        if (brightnessPercent >= 0 && brightnessPercent <= 100) {
            lightConfig.enabled = true;
            lightConfig.brightness = map(brightnessPercent, 0, 100, 0, 255);
        }
    }

    while (mqttClient.available()) {
        mqttClient.read();
    }
}

void disconnectMqtt() {
    if (mqttConnected) {
        mqttClient.stop();
        mqttConnected = false;
    }
}
} // namespace

void initMqtt() {
    mqttClient.onMessage(onMqttMessage);
}

void updateMqtt() {
    if (lightConfig.mqttToken.isEmpty() || WiFi.status() != WL_CONNECTED) {
        disconnectMqtt();
        return;
    }

    if (mqttConnected) {
        mqttClient.poll();
        if (!mqttClient.connected()) {
            mqttConnected = false;
        }
        return;
    }

    const uint32_t now = millis();
    if (now - lastReconnectAttempt < MQTT_RECONNECT_INTERVAL) return;
    lastReconnectAttempt = now;

    mqttClient.setId("lightnode");
    mqttClient.setUsernamePassword(lightConfig.mqttToken.c_str(), "");
    if (mqttClient.connect(MQTT_BROKER, MQTT_PORT)) {
        mqttClient.subscribe(MQTT_TOPIC);
        mqttConnected = true;
        Serial.println("[MQTT] Подключение установлено");
    } else {
        Serial.print("[MQTT] Ошибка подключения: ");
        Serial.println(mqttClient.connectError());
    }
}
