#!/usr/bin/env python3
"""
LightNode color-music bridge daemon for Debian.

Captures system audio (what goes to your Bluetooth speaker) via PulseAudio/PipeWire
monitor (parec), converts loudness + frequency bands to LED color/brightness, and
sends it to LightNode over a persistent WebSocket connection.

Requirements:
    sudo apt install pulseaudio-utils python3-numpy python3-websocket
    pip3 install numpy websocket-client

Setup (Debian):
    sudo cp tools/lightnode-music.service /etc/systemd/system/
    sudo systemctl daemon-reload
    sudo systemctl enable --now lightnode-music.service
"""

import json
import signal
import subprocess
import sys
import time

import numpy as np
import websocket

# ==================== НАСТРОЙКИ ====================
LIGHT_NODE_IP = "192.168.1.98"          # <-- IP вашего LightNode
WS_URL = f"ws://{LIGHT_NODE_IP}/ws"
SAMPLE_RATE = 44100                      # parec отдаёт 44100 Гц по умолчанию
BLOCK_SAMPLES = 1024                     # ~23 мс на блок
BYTES_PER_SAMPLE = 2                     # s16le
BLOCK_BYTES = BLOCK_SAMPLES * BYTES_PER_SAMPLE

# Источник аудио для parec (монитор Bluetooth-колонки).
# Узнать имя можно командой: pactl list sources short
# Обычно это что-то вроде bluez_output.XX_XX_XX_XX_XX_XX.1.monitor
AUDIO_SOURCE = "bluez_output.B8_87_6E_BC_1C_50.1.monitor"

# Чувствительность (подбирается под громкость колонки)
VOLUME_GAIN = 15.0
BRIGHTNESS_MIN = 50
BRIGHTNESS_MAX = 255

# Коэффициенты для FFT-под окрашивания (басы=красный, средние=зелёный, высокие=синий)
BASS_GAIN = 0.5
MID_GAIN = 0.5
HIGH_GAIN = 0.5
# ===================================================

proc = None
running = True
ws_client = None
ws_retry_after = 0.0


def hex_color(r, g, b):
    """0..1 floats -> hex string without #."""
    return "%02x%02x%02x" % (int(r * 255), int(g * 255), int(b * 255))


# Эффекты LightNode (должны совпадать с enum LightEffect в прошивке)
EFFECT_STATIC = 0
EFFECT_MUSIC = 6


def set_effect(effect: int, enabled: bool = True):
    """Переключить эффект на LightNode (RAM only)."""
    send_ws({
        "enabled": enabled,
        "brightness": lightConfig.get("brightness", 128),
        "color": lightConfig.get("color", 0xFFA000),
        "effect": effect,
    })


# Текущий цвет/яркость, который держит мост (чтобы при смене эффекта не сбрасывать)
lightConfig = {"brightness": 128, "color": 0xFFA000}


def send_light(color_hex, brightness, effect=EFFECT_MUSIC):
    """Send the current light frame to LightNode (RAM only)."""
    lightConfig["brightness"] = int(brightness)
    lightConfig["color"] = int(color_hex, 16)
    color = lightConfig["color"]
    send_ws(bytes((
        lightConfig["brightness"],
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF,
        effect,
    )))


def send_ws(payload):
    global ws_client, ws_retry_after

    if ws_client is None:
        now = time.monotonic()
        if now < ws_retry_after:
            return
        try:
            ws_client = websocket.create_connection(WS_URL, timeout=1.5)
        except Exception as exc:
            ws_retry_after = time.monotonic() + 2.0
            print("Ошибка подключения WebSocket к LightNode:", exc, file=sys.stderr)
            return

    try:
        if isinstance(payload, bytes):
            ws_client.send_binary(payload)
        else:
            ws_client.send(json.dumps(payload, separators=(",", ":")))
    except Exception as exc:
        try:
            ws_client.close()
        except Exception:
            pass
        ws_client = None
        ws_retry_after = time.monotonic() + 1.0
        print("Ошибка отправки WebSocket в LightNode:", exc, file=sys.stderr)


def close_ws():
    global ws_client
    if ws_client is not None:
        try:
            ws_client.close()
        except Exception:
            pass
        ws_client = None


def find_audio_source():
    """Пытаемся найти монитор Bluetooth-колонки через pactl. Если не получается — None."""
    try:
        out = subprocess.check_output(["pactl", "list", "sources", "short"], text=True)
        for line in out.splitlines():
            parts = line.split("\t")
            if len(parts) >= 2 and "monitor" in parts[1] and "RUNNING" in line:
                return parts[1]
    except Exception:
        pass
    return None


def start_audio_capture(cmd):
    try:
        return subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except FileNotFoundError:
        print("Ошибка: команда 'parec' не найдена. Установите pulseaudio-utils (Debian).", file=sys.stderr, flush=True)
    except OSError as exc:
        print(f"Не удалось запустить parec: {exc}", file=sys.stderr, flush=True)
    return None


def audio_worker():
    global proc, running

    source = AUDIO_SOURCE
    if not source:
        source = find_audio_source()
    if not source:
        print("Не найден подходящий аудио-источник (монитор колонок).", file=sys.stderr)
        print("Проверьте: pactl list sources short", file=sys.stderr)
        return

    # Запускаем parec с выбранным источником
    cmd = [
        "parec",
        "--device=" + source,
        "--format=s16le",
        f"--rate={SAMPLE_RATE}",
        "--channels=1",
    ]
    proc = start_audio_capture(cmd)
    if proc is None:
        return

    print(f"Аудио-источник: {source}", flush=True)
    print(f"Цветомузыка запущена -> {WS_URL}", flush=True)
    print("Для остановки нажмите Ctrl+C\n", flush=True)

    # Переключаем лампу в эффект Цветомузыка (6)
    set_effect(EFFECT_MUSIC, enabled=True)

    try:
        while running:
            raw = proc.stdout.read(BLOCK_BYTES)
            if len(raw) < BLOCK_BYTES:
                exit_code = proc.poll()
                if exit_code is not None:
                    error_output = proc.stderr.read().decode("utf-8", errors="replace").strip()
                    print(
                        f"parec завершился с кодом {exit_code}"
                        + (f": {error_output}" if error_output else "; перезапускаю захват"),
                        file=sys.stderr,
                        flush=True,
                    )
                    if not running:
                        break
                    time.sleep(2.0)
                    proc = start_audio_capture(cmd)
                    if proc is None:
                        return
                    continue
                # Нет аудио (пауза в музыке или источник приостановлен) — ждём и пробуем снова
                time.sleep(0.05)
                continue

            samples = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
            if samples.size == 0:
                continue

            # 1. Громкость -> яркость
            rms = np.sqrt(np.mean(samples ** 2))
            volume = min(1.0, rms * VOLUME_GAIN)
            brightness = int(BRIGHTNESS_MIN + volume * (BRIGHTNESS_MAX - BRIGHTNESS_MIN))

            # 2. FFT -> цвет (басы=красный, средние=зелёный, высокие=синий)
            fft = np.abs(np.fft.rfft(samples))
            freqs = np.fft.rfftfreq(len(samples), 1 / SAMPLE_RATE)

            def band(fmin, fmax):
                mask = (freqs > fmin) & (freqs < fmax)
                return float(np.mean(fft[mask])) if np.any(mask) else 0.0

            r = band(20, 150) * BASS_GAIN
            g = band(150, 1000) * MID_GAIN
            b = band(1000, 6000) * HIGH_GAIN
            peak = max(r, g, b)
            if peak > 0.0:
                scale = 1.0 / peak
                r *= scale
                g *= scale
                b *= scale

            send_light(hex_color(r, g, b), brightness, effect=EFFECT_MUSIC)

    finally:
        # Возвращаем лампу в обычный статический режим
        set_effect(EFFECT_STATIC, enabled=True)
        close_ws()
        if proc:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()


def handle_signal(signum, frame):
    global running
    running = False


def main():
    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)
    audio_worker()


if __name__ == "__main__":
    main()
