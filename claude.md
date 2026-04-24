# claude.md — IoT Smart Monitor — Comprehensive Documentation

## Table of Contents
1. [System Overview](#1-system-overview)
2. [Architecture Diagram](#2-architecture-diagram)
3. [Module Explanations](#3-module-explanations)
4. [Setup Instructions](#4-setup-instructions)
5. [Firebase Schema](#5-firebase-schema)
6. [State Machine Flowchart](#6-state-machine-flowchart)
7. [TinyML Pipeline](#7-tinyml-pipeline)
8. [Scaling Strategy](#8-scaling-strategy)
9. [Troubleshooting](#9-troubleshooting)
10. [Future Improvements](#10-future-improvements)

---

## 1. System Overview

This project is a production-grade IoT monitoring and control system with three main layers:

**Edge (ESP32 firmware)**
- Reads temperature, humidity, and light sensors
- Runs a pre-trained neural network on-device (no cloud call needed for inference)
- Implements a strict finite state machine to prevent race conditions
- Buffers data locally when WiFi is unavailable
- Controls a relay (fan/heater) and status LED automatically

**Cloud (Firebase Realtime Database)**
- Stores device state, time-series readings, and alert events
- Acts as the message bus between ESP32 devices and the web dashboard
- Provides authentication and security rules

**Frontend (Web Dashboard)**
- Displays live sensor charts updated in real-time via WebSocket
- Shows per-device status cards with FSM state
- Lets operators manually override actuators
- Provides an alert timeline and device health table

---

## 2. Architecture Diagram

```
  ┌──────────────────────────────────────────────────────────────────────────┐
  │  EDGE LAYER                                                              │
  │                                                                          │
  │  ┌──────────────────────────────────────────────────────────────────┐    │
  │  │  ESP32 Node (×N)                                                 │    │
  │  │                                                                  │    │
  │  │  GPIO ─► SensorManager ─► MLInference ─► StateManager           │    │
  │  │                                              │                   │    │
  │  │                                              ▼                   │    │
  │  │                                       ActuatorController         │    │
  │  │                                              │                   │    │
  │  │                                              ▼                   │    │
  │  │                                       FirebaseManager            │    │
  │  │                                       (+ offline queue)          │    │
  │  └──────────────────────────────────────────────────────────────────┘    │
  │                         │ WiFi / HTTPS                                   │
  └─────────────────────────┼────────────────────────────────────────────────┘
                            │
  ┌─────────────────────────▼────────────────────────────────────────────────┐
  │  CLOUD LAYER                                                             │
  │                                                                          │
  │  Firebase Realtime Database                                              │
  │  /devices/{id}/latest      ← current reading                            │
  │  /devices/{id}/heartbeat   ← device alive ping                          │
  │  /devices/{id}/commands    ← dashboard → device control                 │
  │  /readings/{id}/{pushId}   ← time-series log                            │
  │  /alerts/{id}/{pushId}     ← alert events                               │
  │                                                                          │
  │  Firebase Authentication   ← per-device email/password                  │
  └─────────────────────────────────────────┬────────────────────────────────┘
                                            │ WebSocket / HTTPS
  ┌─────────────────────────────────────────▼────────────────────────────────┐
  │  FRONTEND LAYER                                                          │
  │                                                                          │
  │  index.html + styles.css + app.js                                        │
  │  ├── Real-time charts (Chart.js)                                         │
  │  ├── Device status cards (FSM state)                                     │
  │  ├── Alert timeline                                                      │
  │  ├── Manual relay controls                                               │
  │  └── Device health table                                                 │
  └──────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Module Explanations

### StateManager (`firmware/esp32_sensor_node/StateManager.h/.cpp`)
Implements the finite state machine. Maintains the current `DeviceState` enum and enforces a legal transition table. Every transition is logged to Serial with the elapsed time in the previous state.

**Why a FSM?** Without it, WiFi reconnection, alert escalation, and sensor retries all race against each other. The FSM ensures each state has a single, predictable behaviour.

### ConfigManager (`ConfigManager.h/.cpp`)
Loads `config.json` from SPIFFS at boot. Any field missing from the file falls back to a compile-time default. This allows a single firmware binary to be flashed to every device — only the per-device `config.json` changes.

After an OTA config update from Firebase, the new values can be saved back to SPIFFS via `save()`.

### SensorManager (`SensorManager.h/.cpp`)
Wraps the DHT library with:
- **Retry logic** (up to 3 attempts, 100 ms apart)
- **Plausibility check** (rejects -40°C and NaN)
- **Exponential Moving Average** smoothing (configurable α)
- **Mock mode** (`#define SENSOR_MOCK`) for bench testing without hardware

### ActuatorController (`ActuatorController.h/.cpp`)
Controls relay and LED independently. Key feature: **override expiry**. When the dashboard sends a manual relay command, it's stored with a timeout (default 5 minutes). After expiry the relay returns to its last FSM-driven state. This prevents stuck actuators if the dashboard goes offline.

LED patterns encode system state:
- Slow blink (1 Hz) → monitoring normally
- Fast blink (5 Hz) → alert or connecting
- SOS pattern → unrecoverable error

### MLInference (`MLInference.h/.cpp`)
Loads the TFLite Micro model from the C byte array in `tinyml_model.h`. Normalises inputs to [0,1] before inference. Outputs a composite **risk score** (0.0–1.0) computed as:

```
risk_score = p_warning × 0.5 + p_critical × 1.0
```

If inference fails (e.g. stub model), the firmware falls back to threshold-only rules.

### FirebaseManager (`FirebaseManager.h/.cpp`)
Uses the `Firebase_ESP_Client` library. On each cycle:
1. Checks if Firebase is `ready()` (authenticated + connected)
2. If online: flushes the offline queue, then pushes the new reading to `/devices/{id}/latest` and appends to `/readings/{id}/`
3. If offline: enqueues the reading in a ring buffer (30 entries max; oldest dropped on overflow)
4. Polls `/devices/{id}/commands/relayOverride` every 5 seconds

### esp32_sensor_node.ino
The main orchestrator. Contains `setup()` (FSM boot sequence) and `loop()` (per-cycle orchestration). **No business logic lives here** — it only calls module APIs and handles WiFi watchdog + error recovery.

---

## 4. Setup Instructions

### Prerequisites

| Tool | Purpose | Install |
|---|---|---|
| Arduino IDE 2.x | Flash firmware | arduino.cc |
| ESP32 board support | Arduino ESP32 | Board Manager: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` |
| Python 3.9+ | ML training | python.org |
| Git | Version control | git-scm.com |

### Step 1 — Clone the repo
```bash
git clone https://github.com/ajjajay/iot_EL.git
cd iot_EL
```

### Step 2 — Install Python dependencies and train model
```bash
bash scripts/setup.sh
# OR manually:
pip install -r models/requirements.txt
python models/mock_data_generator.py
python models/model_training.py --epochs 100
python models/model_converter.py
# → generates firmware/esp32_sensor_node/tinyml_model.h
```

### Step 3 — Configure Firebase
Follow `scripts/firebase_setup.md` step by step. Key outputs:
- Firebase project created
- Realtime Database enabled
- Auth users created (one per device + dashboard)
- Web app config keys copied

### Step 4 — Create device config.json files
One file per ESP32. Place at `firmware/esp32_sensor_node/data/config.json`:
```json
{
  "deviceId":    "esp32_node_01",
  "location":    "Lab Room A",
  "wifiSsid":    "YourNetwork",
  "wifiPassword":"YourPassword",
  "firebaseApiKey":    "YOUR_KEY",
  "firebaseUrl":       "https://YOUR-PROJECT.firebaseio.com",
  "firebaseEmail":     "esp32-node-01@yourdomain.com",
  "firebasePass":      "DevicePassword",
  "dhtPin":      4,
  "ldrPin":      34,
  "relayPin":    26,
  "ledPin":      2
}
```

### Step 5 — Install Arduino libraries
Open Arduino IDE → Sketch → Include Library → Manage Libraries:
- `DHT sensor library` (Adafruit)
- `Adafruit Unified Sensor`
- `Firebase ESP Client` (mobizt)
- `ArduinoJson` ≥ 6.x
- `TensorFlowLite_ESP32`

### Step 6 — Upload SPIFFS and firmware
1. Install ESP32 SPIFFS uploader plugin (see: https://github.com/me-no-dev/arduino-esp32fs-plugin)
2. Tools → ESP32 Sketch Data Upload (uploads `data/config.json`)
3. Verify + Upload the main sketch

### Step 7 — Configure and open dashboard
1. Edit `frontend/app.js` → replace `FIREBASE_CONFIG` with your project values
2. Replace `DASH_EMAIL` / `DASH_PASSWORD` with your dashboard Auth user
3. Open `frontend/index.html` in Chrome/Firefox
4. Without Firebase config → auto demo mode with simulated data

---

## 5. Firebase Schema

```
/
├── devices/
│   └── {deviceId}/
│       ├── meta/
│       │   ├── deviceId      (string)
│       │   ├── location      (string)
│       │   ├── firmware      (string)
│       │   └── registeredAt  (number — unix seconds)
│       ├── online            (boolean)
│       ├── latest/           ← overwritten each cycle
│       │   ├── temperatureC  (float)
│       │   ├── humidityPct   (float)
│       │   ├── lightRaw      (int, 0–4095)
│       │   ├── lightNorm     (float, 0–1)
│       │   ├── riskScore     (float, 0–1)
│       │   ├── mlLabel       (int, 0=normal/1=warning/2=critical)
│       │   ├── pNormal       (float)
│       │   ├── pWarning      (float)
│       │   ├── pCritical     (float)
│       │   ├── state         (string — FSM state name)
│       │   └── ts            (number — JS millis)
│       ├── heartbeat/
│       │   ├── ts            (number — unix seconds)
│       │   ├── state         (string)
│       │   ├── heapFree      (number — bytes)
│       │   └── uptime        (number — seconds)
│       ├── commands/
│       │   └── relayOverride (string — "ON"/"OFF"/"AUTO")
│       └── config/           ← remote threshold overrides
│           ├── tempWarningC
│           ├── tempCriticalC
│           ├── humidityWarningPct
│           └── mlRiskThreshold
│
├── readings/
│   └── {deviceId}/
│       └── {pushId}/         ← auto-keyed, time-ordered
│           └── (same fields as latest/)
│
└── alerts/
    └── {deviceId}/
        └── {pushId}/
            ├── deviceId
            ├── riskScore
            ├── mlLabel
            ├── temperatureC
            ├── ts
            └── acknowledged  (boolean)
```

---

## 6. State Machine Flowchart

```
                       Power on
                          │
                          ▼
                    ┌──────────┐
                    │   INIT   │   Hardware init, config load, ML load
                    └────┬─────┘
                         │ OK
                         ▼
                    ┌──────────┐
                    │CONNECTING│   WiFi.begin() + Firebase.begin()
                    └────┬─────┘
                         │ connected (or timeout → offline mode)
                         ▼
                    ┌──────────┐
                    │  READY   │   All modules up, first heartbeat sent
                    └────┬─────┘
                         │ immediate
                         ▼
         ┌──────────────────────────────────┐
         │                                  │
         ▼                                  │
    ┌──────────┐    riskScore ≥ 0.6         │
    │MONITORING│ ──────────────────────►  ┌──────┐
    │          │                           │ALERT │
    │          │ ◄────────────────────── └──────┘
    └──────────┘    riskScore < 0.6 (cooldown)
         │
         │ WiFi lost
         ▼
    ┌──────────┐
    │CONNECTING│   reconnect attempt (20 s timeout)
    └──────────┘

    Any state
         │ sensor fails 5×, or critical fault
         ▼
    ┌──────────┐
    │  ERROR   │   SOS LED, wait 10 s, ESP.restart()
    └──────────┘
```

**Actuator behaviour by state:**

| State | Relay | LED |
|---|---|---|
| INIT | OFF | Fast blink |
| CONNECTING | OFF | Fast blink |
| READY | OFF | Slow blink |
| MONITORING | OFF | Slow blink |
| ALERT | ON | Fast blink |
| ERROR | OFF | SOS pattern |

---

## 7. TinyML Pipeline

### Model Architecture
```
Input(3)   →  Dense(16, ReLU)  →  Dense(8, ReLU)  →  Dense(3, Softmax)
~700 parameters, ~4.2 KB TFLite, ~8 KB tensor arena on ESP32
```

### Input Normalisation (must match MLInference.h constants)
```python
temp_norm  = (temp_c - (-10)) / (60 - (-10))   # TEMP_MIN=-10, TEMP_MAX=60
hum_norm   = hum_pct / 100.0
light_norm = light_norm  # already [0,1] from ADC
```

### Training Pipeline
```
mock_data_generator.py
    ↓ CSV (5000 samples, 50/30/20% split normal/warning/critical)
model_training.py
    ↓ Keras → EarlyStopping → TFLite converter → model.tflite
model_converter.py
    ↓ bytes → C hex array → tinyml_model.h
ESP32 firmware
    ↓ tflite::GetModel(g_model) → AllocateTensors() → Invoke()
```

### Retraining Workflow
1. Collect real sensor data → save to CSV with `label` column
2. `python models/model_training.py --data real_data.csv --epochs 150`
3. `python models/model_converter.py`
4. Copy new `tinyml_model.h` into `firmware/esp32_sensor_node/`
5. Recompile and flash all devices

---

## 8. Scaling Strategy

### From 2 to 20 devices

**No schema changes needed.** Firebase auto-discovers new devices when they register under `/devices/{newId}`. The dashboard's `onValue("/devices")` listener picks them up automatically.

Per-device differentiation via `config.json` in SPIFFS — flash the same binary to all boards.

### Database read cost optimisation (for 20+ devices)

Currently the dashboard subscribes to the full `/devices` subtree. For large deployments:
1. Use per-device listeners: `db.ref('/devices/' + id).on('value', ...)`
2. Limit time-series reads: `.limitToLast(50)` on `/readings/{id}`
3. Consider Firebase Cloud Functions to aggregate stats server-side

### Multi-region deployments

Firebase Realtime Database is single-region. For true multi-region IoT:
- Use Firebase Firestore (multi-region support) instead of RTDB
- Or deploy separate Firebase projects per region + a meta-dashboard

### OTA firmware updates

Add `ArduinoOTA` library to the firmware and trigger updates from Firebase:
1. Push new firmware version to `/devices/{id}/commands/otaUrl`
2. Firmware polls this field and starts HTTPUpdate if version differs

---

## 9. Troubleshooting

See `docs/TROUBLESHOOTING.md` for the full guide.

**Top 5 issues:**
1. CONNECTING loop → wrong WiFi credentials or Firebase URL
2. NaN sensor readings → missing DHT pull-up resistor (10kΩ DATA→3.3V)
3. ML inference fails → stub tinyml_model.h; run training scripts
4. Firebase permission denied → using prod.rules without auth UID matching
5. Dashboard demo mode → FIREBASE_CONFIG still has placeholder values

---

## 10. Future Improvements

| Feature | Effort | Value |
|---|---|---|
| OTA firmware updates | Medium | High — no physical access to update |
| INT8 model quantization | Low | Reduces model 4× in size, faster inference |
| Firebase Cloud Functions for alert aggregation | Medium | Offload logic from devices |
| MQTT broker alternative | High | Lower latency than Firebase polling |
| Edge federated learning | Very High | Improve model from fleet data without centralising it |
| Battery + deep sleep mode | Medium | Solar/battery-powered outdoor nodes |
| Multi-sensor fusion | Low | Add CO2, PIR, pressure sensors via same SensorManager |
| PlatformIO migration | Low | Better dependency management than Arduino IDE |
| Grafana + InfluxDB export | Medium | For production-grade time-series analytics |
| Per-device anomaly detection baseline | High | Self-calibrating thresholds using device history |
