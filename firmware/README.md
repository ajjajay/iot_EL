# Firmware — ESP32 Sensor Node

## Overview

Single Arduino sketch (`esp32_sensor_node/`) that runs on any ESP32.
Flash the same binary to all nodes; differentiate them via per-device `config.json` uploaded to SPIFFS.

## File Map

| File | Role |
|---|---|
| `esp32_sensor_node.ino` | Entry point; FSM orchestrator |
| `StateManager.h/.cpp` | Finite state machine (INIT→CONNECTING→READY→MONITORING⇄ALERT) |
| `ConfigManager.h/.cpp` | JSON config from SPIFFS + compile-time fallbacks |
| `SensorManager.h/.cpp` | DHT22 + LDR reads with EMA smoothing & retry |
| `ActuatorController.h/.cpp` | Relay + LED (with override expiry) |
| `FirebaseManager.h/.cpp` | Firebase push + offline ring-buffer queue |
| `MLInference.h/.cpp` | TFLite Micro wrapper |
| `tinyml_model.h` | Embedded model byte array |

## Required Libraries

Install via Arduino Library Manager or PlatformIO:

```
DHT sensor library         (Adafruit)
Adafruit Unified Sensor    (Adafruit)
Firebase ESP Client        (mobizt)
ArduinoJson                (Benoit Blanchon)  >= 6.x
TensorFlowLite_ESP32       (TensorFlow)
```

## Board Settings (Arduino IDE)

| Setting | Value |
|---|---|
| Board | ESP32 Dev Module |
| CPU Frequency | 240 MHz |
| Flash Size | 4 MB |
| Partition Scheme | Default 4MB with spiffs |
| Upload Speed | 921600 |

## Quick Start

1. **Install libraries** (see above)
2. **Copy `.env.example` → `config.json`** with your credentials
3. **Upload SPIFFS** — use Arduino ESP32 SPIFFS Uploader plugin  
   Place `config.json` in `esp32_sensor_node/data/config.json`
4. **Generate model** — `cd ../models && python model_training.py && python model_converter.py`
5. **Compile & upload** the sketch
6. **Open Serial Monitor** at 115200 baud to watch FSM state transitions

## Mock Mode (no hardware)

Add `#define SENSOR_MOCK` at the top of `SensorManager.cpp` to enable
simulated sensor data — useful for bench testing the Firebase/ML pipeline
without physical sensors.

## Wiring

```
ESP32 GPIO 4  → DHT22 Data
ESP32 GPIO 34 → LDR (voltage divider to GND)
ESP32 GPIO 26 → Relay IN
ESP32 GPIO 2  → Onboard LED (or external)
3.3 V / GND   → DHT22, LDR pull-up resistor
```

## Adding a Second Device

1. Duplicate `config.json`, change `deviceId` → `esp32_node_02`
2. Upload to second ESP32's SPIFFS
3. Flash same firmware — it auto-registers under the new ID in Firebase
