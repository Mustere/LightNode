# LightNode

LightNode is an open-source WS2812 LED strip controller for the ESP8266
Wemos D1 Mini. It provides a local web dashboard, animated effects, sunrise
automation, MQTT control, persistent settings, and OTA firmware updates.

The project is built with PlatformIO and does not require a cloud service for
normal web-panel operation.

## Features

- WS2812 control on pin `D5` for 120 LEDs by default;
- on/off control, RGB color, and brightness;
- static, rainbow, fire, waves, twinkle, and color-cycle effects;
- non-blocking animations and non-blocking sunrise test;
- optional daily sunrise schedule;
- Wi-Fi station (STA) and access-point (AP) modes;
- recovery AP when the configured STA network is unavailable;
- persistent configuration stored on LittleFS;
- MQTT control through a Flespi token;
- OTA firmware upload from the web panel.

## Hardware

| Component | Default |
|---|---|
| Controller | Wemos D1 Mini / ESP8266 |
| LED data pin | `D6` |
| LED count | `120` |
| LED protocol | WS2812 / NeoPixel |
| Serial monitor | `115200 baud` |

The LED strip must have an appropriate power supply. Connect the controller
ground to the strip ground. Do not power a large strip directly from the
Wemos 5 V pin.

To use another pin or LED count, edit the constants at the top of
[`src/LightManager.cpp`](src/LightManager.cpp).

## Repository structure

```text
LightNode/
├── src/
│   ├── main.cpp            Application setup and loop
│   ├── LightManager.cpp    WS2812 rendering and sunrise logic
│   ├── MqttManager.cpp     MQTT connection and command handling
│   ├── ConfigManager.cpp   LittleFS configuration persistence
│   ├── TimeManager.cpp     NTP time helpers
│   └── WebInterface.cpp    HTTP API, web server, OTA
├── include/                Public module headers
├── data/                   Web panel uploaded to LittleFS
├── platformio.ini          PlatformIO environment and dependencies
└── LICENSE
```

The `data/config.json` file is intentionally not tracked. The device creates
it on first boot and updates it when settings are saved.

## Requirements

- VS Code with the PlatformIO extension, or PlatformIO Core CLI;
- a Wemos D1 Mini / compatible ESP8266 board;
- a WS2812-compatible LED strip;
- a USB data cable.

## Build and upload

Build the firmware:

```bash
pio run
```

Upload the firmware:

```bash
pio run --target upload
```

Upload the web panel to LittleFS:

```bash
pio run --target uploadfs
```

Open the serial monitor:

```bash
pio device monitor
```

The project uses the `d1_mini` environment from [`platformio.ini`](platformio.ini).
The firmware is configured for a 160 MHz CPU clock and LittleFS.

## First boot and Wi-Fi

If no configuration exists, the device starts an access point:

- SSID: `LightNode_AP`
- Password: `12345678`
- Panel: `http://192.168.4.1`

Use the web panel to configure either:

- **STA** — connect to an existing Wi-Fi network;
- **AP** — keep operating as a standalone access point.

If STA connection fails during startup, LightNode starts the recovery AP
`LightNode_RECOVERY` with password `12345678`.

## Web panel

Light settings are applied automatically shortly after a control changes.
The panel provides:

- immediate LED enable/disable;
- brightness, color, and effect selection;
- sunrise test;
- daily sunrise times for each weekday;
- Wi-Fi credentials and MQTT token;
- OTA firmware upload.

The Wi-Fi form intentionally causes a reboot after saving because the network
configuration must be reinitialized.

## MQTT

When an MQTT token is configured and the device has an active STA connection,
LightNode connects to:

```text
Broker: mqtt.flespi.io
Port: 1883
Topic: inTopic
```

The token is entered in the web panel and stored in the device configuration.
Commands sent to `inTopic` are:

| Payload | Action |
|---|---|
| `0` | Turn the strip off |
| `1` | Turn the strip on |
| `0`–`100` | Turn on and set brightness as a percentage |

The client retries a failed connection every 10 seconds.

## HTTP API

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/get-config` | Return Wi-Fi and light settings |
| `GET` | `/api/get-status` | Return current light state |
| `POST` | `/api/set-light-enabled` | Immediately enable or disable the strip |
| `POST` | `/api/save-light` | Save light and sunrise settings |
| `POST` | `/api/test-sunrise` | Start a non-blocking sunrise test |
| `POST` | `/api/save-wifi` | Save Wi-Fi settings and reboot |
| `POST` | `/api/update` | Upload firmware using OTA |
| `GET` | `/heap` | Return free heap for diagnostics |

Example:

```bash
curl -X POST http://192.168.4.1/api/set-light-enabled \
  -H 'Content-Type: application/json' \
  -d '{"enabled":false}'
```

## Configuration and secrets

Runtime settings are stored in `/config.json` on LittleFS. They include
Wi-Fi credentials and the MQTT token. Do not commit a real configuration,
token, password, or firmware dump to the repository.

The repository contains only the default AP credentials used for first boot.
Change them on the device before deploying it.

## License

This project is distributed under the GNU General Public License v3. See
[`LICENSE`](LICENSE).
