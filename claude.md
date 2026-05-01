# CLAUDE.md — Iris Biometric Access Control System

## Table of Contents
1. [System Overview](#1-system-overview)
2. [Architecture Diagram](#2-architecture-diagram)
3. [Module Explanations](#3-module-explanations)
4. [Setup Instructions](#4-setup-instructions)
5. [Firebase Schema](#5-firebase-schema)
6. [State Machine Flowchart](#6-state-machine-flowchart)
7. [Biometric Pipeline](#7-biometric-pipeline)
8. [TinyML Pipeline (Ambient)](#8-tinyml-pipeline-ambient)
9. [Scaling Strategy](#9-scaling-strategy)
10. [Troubleshooting](#10-troubleshooting)
11. [Future Improvements](#11-future-improvements)

---

## 1. System Overview

This project is a production-grade IoT biometric access control system with three main layers:

**Edge (ESP32-CAM firmware)**
- Captures iris images via OV2640 camera and extracts a 64-element feature vector
- Matches against enrolled templates stored in SPIFFS using normalised RMS distance
- Detects anomalous sign-in patterns (brute force, spoofing attempts) with a sliding-window scorer
- Controls a relay (door lock) and status LED based on authentication result
- Runs an ambient environmental risk inference (TFLite Micro) as a secondary monitor
- Publishes biometric events and alerts to both Firebase and AWS IoT Core over MQTT/TLS

**Cloud (Firebase + AWS)**
- Firebase Realtime Database stores device state, sign-in history, enrolled users, and alerts
- AWS IoT Core routes biometric alerts via IoT Rules → Lambda → AWS Bedrock Agent → SNS user notification
- Bedrock Agent publishes acknowledgement back to the device to close the feedback loop

**Frontend (Web Dashboard)**
- Displays live sign-in logs, enrolled users, and security alerts
- Sends enrollment commands to devices and shows real-time ambient sensor charts
- Manual relay override with 5-minute auto-expiry

---

## 2. Architecture Diagram

```
  ┌──────────────────────────────────────────────────────────────────────────┐
  │  EDGE LAYER                                                              │
  │                                                                          │
  │  ┌──────────────────────────────────────────────────────────────────┐    │
  │  │  ESP32-CAM Node (×N)                                             │    │
  │  │                                                                  │    │
  │  │  OV2640 → IrisCamera → BiometricManager → AnomalyDetector        │    │
  │  │                              │                    │              │    │
  │  │                              ▼                    ▼              │    │
  │  │                        StateManager         AlertManager         │    │
  │  │                              │                    │              │    │
  │  │  DHT22/LDR → SensorManager → MLInference          │              │    │
  │  │                              │               ActuatorController  │    │
  │  │                              ▼                    │              │    │
  │  │                       FirebaseManager        AWSIoTManager       │    │
  │  └──────────────────────────────────────────────────────────────────┘    │
  │           │ WiFi / HTTPS                      │ MQTT/TLS 8883           │
  └───────────┼───────────────────────────────────┼──────────────────────────┘
              │                                   │
  ┌───────────▼─────────────┐      ┌──────────────▼──────────────────────────┐
  │  Firebase RTDB          │      │  AWS IoT Core                           │
  │  /devices/{id}/latest   │      │  Topics: iot/{thing}/biometric/signin   │
  │  /signins/{id}/         │      │          iot/{thing}/biometric/alert    │
  │  /users/{userId}        │      │          iot/{thing}/ai/alerts          │
  │  /alerts/{id}/          │      │  Rules → Lambda BiometricAlertAgent     │
  │  /readings/{id}/        │      │         → Bedrock Agent → SNS → User    │
  └───────────┬─────────────┘      └─────────────────────────────────────────┘
              │ WebSocket
  ┌───────────▼──────────────────────────────────────────────────────────────┐
  │  Web Dashboard (frontend/)                                               │
  │  Sign-in log · Enrolled users · Security alerts · Sensor charts          │
  │  Enrollment command panel · Manual door controls · Device health table   │
  └──────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Module Explanations

### IrisCamera (`firmware/esp32_sensor_node/IrisCamera.h/.cpp`)
Wraps the OV2640 driver (AI Thinker ESP32-CAM default pins). `capture()` takes a grayscale frame and runs `_extractFeatures()`: the image is divided into an 8×8 grid and the mean pixel intensity of each cell is stored as a float32, producing a 64-element descriptor. `captureAverage()` accumulates N valid frames and returns their mean, improving stability across lighting variation.

**Why 8×8 zonal descriptor?** Zero external dependencies, fits on-chip SRAM, requires no segmentation. Production systems would use Gabor wavelets or a CNN embedding, but this is sufficient for proof-of-concept gating with consistent lighting.

### BiometricManager (`BiometricManager.h/.cpp`)
Manages enrolled user templates in SPIFFS. Templates are stored as raw binary float32 arrays (`/bio/{userId}_t{N}.bin`). The user registry is a JSON file at `/bio/users.json`. `match()` computes normalised RMS distance between the query descriptor and every stored template; the closest match below threshold wins. Ring-buffer overwrite handles the 5-template-per-user limit.

### AnomalyDetector (`AnomalyDetector.h/.cpp`)
Maintains a sliding window of the last 20 sign-in events. On each `record()` call it computes a composite anomaly score:
```
0.40 × failure_rate + 0.35 × score_proximity + 0.25 × frequency_spike
```
- **failure_rate**: fraction of failed attempts in the window
- **score_proximity**: how close the best match score is to the threshold (spoofing signal — a score just below threshold suggests a partial replay)
- **frequency_spike**: > 5 events in 60 s → 1.0

### AlertManager (`AlertManager.h/.cpp`)
Dispatches alerts to both AWS IoT (MQTT JSON) and Firebase when the anomaly score exceeds `anomalyScoreThreshold`. Suppresses duplicate alerts of the same type within `alertCooldownMs` (default 30 s). Uses forward-declared pointers to avoid circular includes.

### StateManager (`StateManager.h/.cpp`)
Implements the finite state machine. Maintains the current `DeviceState` enum and enforces a legal transition table. Every transition is logged to Serial with elapsed time in the previous state.

**States**: INIT, CONNECTING, READY, MONITORING, ALERT, ERROR, ENROLLING, AUTHENTICATING, AUTHENTICATED, REJECTED

### ConfigManager (`ConfigManager.h/.cpp`)
Loads `config.json` from SPIFFS at boot. Missing fields fall back to compile-time defaults. Supports all biometric parameters (`irisMatchThreshold`, `anomalyScoreThreshold`, `authButtonPin`, etc.) alongside existing env-sensor thresholds. A single firmware binary can be flashed to every device — only the per-device `config.json` changes.

### SensorManager (`SensorManager.h/.cpp`)
Wraps the DHT library with retry logic (3 attempts), plausibility checks, and Exponential Moving Average smoothing (α=0.3). Mock mode available via `#define SENSOR_MOCK`.

### ActuatorController (`ActuatorController.h/.cpp`)
Controls relay and LED independently. Override expiry: dashboard manual commands auto-expire after 5 minutes, preventing stuck-open doors. LED patterns encode system state (slow blink = idle, fast blink = active/alert, SOS = error).

### MLInference (`MLInference.h/.cpp`)
Loads the TFLite Micro model for ambient environmental risk. Secondary to biometrics — runs in the background loop and triggers an ALERT state if `riskScore ≥ mlRiskThreshold`.

### FirebaseManager (`FirebaseManager.h/.cpp`)
Uses the `Firebase_ESP_Client` library. Pushes ambient readings, sign-in events, enrollment records, and biometric alerts to separate RTDB paths. Offline ring buffer (30 entries) prevents data loss during WiFi outages. Polls `/devices/{id}/commands/enroll/pending` every 5 s to trigger enrollment.

### AWSIoTManager (`AWSIoTManager.h/.cpp`)
MQTT/TLS client via PubSubClient on port 8883. Subscribes to three topics: commands, shadow delta, and `iot/{thing}/ai/alerts` (Bedrock ACK). `publishBiometricEvent()` and `publishAlertJson()` handle outbound telemetry. `getAgentAck()` returns true once the Lambda has confirmed the alert was routed.

---

## 4. Setup Instructions

### Prerequisites

| Tool | Purpose | Install |
|---|---|---|
| Arduino IDE 2.x | Flash firmware | arduino.cc |
| ESP32 board support | Arduino ESP32 | Board Manager: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` |
| ESP32 SPIFFS Uploader | Upload config.json | github.com/me-no-dev/arduino-esp32fs-plugin |
| Python 3.9+ | ML training (ambient model) | python.org |

### Step 1 — Clone the repo
```bash
git clone https://github.com/ajjajay/iot_EL.git
cd iot_EL
```

### Step 2 — Configure Firebase
Follow `scripts/firebase_setup.md` step by step:
- Create Firebase project + Realtime Database
- Enable Email/Password auth; create one user per device + dashboard user
- Apply security rules from `firebase/prod.rules`
- Copy the web app `firebaseConfig` object

### Step 3 — Configure AWS IoT Core
Follow steps 8 in `scripts/firebase_setup.md`:
- Create a Thing per device
- Download certificates and paste into `firmware/esp32_sensor_node/aws_certificates.h`
- Create and attach an IoT Policy
- Create two IoT Rules (sign-in telemetry + anomaly alert routing to Lambda)
- Deploy `BiometricAlertAgent` Lambda function

### Step 4 — Create device config.json files
One file per ESP32-CAM. Place at `firmware/esp32_sensor_node/data/config.json`:
```json
{
  "deviceId":    "esp32_node_01",
  "location":    "Entrance A",
  "wifiSsid":    "YourNetwork",
  "wifiPassword":"YourPassword",
  "firebaseApiKey":    "YOUR_KEY",
  "firebaseUrl":       "https://YOUR-PROJECT.firebaseio.com",
  "firebaseEmail":     "esp32-node-01@yourdomain.com",
  "firebasePass":      "DevicePassword",
  "dhtPin":      4,
  "ldrPin":      34,
  "relayPin":    26,
  "ledPin":      2,
  "dhtType":     22,
  "authButtonPin":       15,
  "irisMatchThreshold":  0.30,
  "irisEnrollFrames":    5,
  "authDisplayMs":       3000,
  "anomalyScoreThreshold": 0.60,
  "alertCooldownMs":     30000,
  "awsEnabled":          true,
  "awsEndpoint":         "xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com",
  "awsThingName":        "esp32_node_01"
}
```

### Step 5 — Install Arduino libraries
Open Arduino IDE → Sketch → Include Library → Manage Libraries:
- `DHT sensor library` (Adafruit)
- `Adafruit Unified Sensor`
- `Firebase ESP Client` (mobizt)
- `ArduinoJson` ≥ 6.x
- `PubSubClient` (knolleary)
- `TensorFlowLite_ESP32`

### Step 6 — Upload SPIFFS and firmware
1. **Partition scheme**: Tools → Partition Scheme → **Huge APP** (3 MB app / 1 MB SPIFFS)
2. **Board**: Tools → Board → AI Thinker ESP32-CAM
3. Tools → ESP32 Sketch Data Upload (uploads `data/config.json`)
4. Verify + Upload the main sketch

### Step 7 — Train ambient ML model (optional)
The firmware falls back to threshold-only rules if the stub model is used:
```bash
pip install -r models/requirements.txt
python models/mock_data_generator.py
python models/model_training.py --epochs 100
python models/model_converter.py
# → generates firmware/esp32_sensor_node/tinyml_model.h
```

### Step 8 — Configure and open dashboard
1. Edit `frontend/app.js` → replace `FIREBASE_CONFIG` with your project values
2. Replace `DASH_EMAIL` / `DASH_PASSWORD` with your dashboard Auth user
3. Open `frontend/index.html` in Chrome/Firefox
4. Without Firebase config → auto demo mode with simulated biometric data

### Step 9 — Enroll users
1. Open the dashboard → Enrolled Users → Enroll New User
2. Select device, enter User ID and Full Name, click Send Enrollment Command
3. The user looks at the camera; device captures 5 averaged frames within 30 s
4. Template saved to device SPIFFS; enrollment record pushed to Firebase `/users/`

---

## 5. Firebase Schema

```
/
├── devices/
│   └── {deviceId}/
│       ├── meta/
│       │   ├── deviceId      (string)
│       │   ├── location      (string)
│       │   ├── firmware      (string — "2.0.0")
│       │   └── registeredAt  (number — unix seconds)
│       ├── online            (boolean)
│       ├── latest/           ← overwritten each ambient cycle
│       │   ├── temperatureC  (float)
│       │   ├── humidityPct   (float)
│       │   ├── lightRaw      (int, 0–4095)
│       │   ├── lightNorm     (float, 0–1)
│       │   ├── riskScore     (float, 0–1)
│       │   ├── mlLabel       (int, 0=normal/1=warning/2=critical)
│       │   ├── state         (string — FSM state name)
│       │   └── ts            (number — JS millis)
│       ├── heartbeat/
│       │   ├── ts            (number — unix seconds)
│       │   ├── state         (string)
│       │   ├── heapFree      (number — bytes)
│       │   └── uptime        (number — seconds)
│       ├── commands/
│       │   ├── relayOverride (string — "ON"/"OFF"/"AUTO")
│       │   └── enroll/
│       │       ├── userId    (string)
│       │       ├── name      (string)
│       │       └── pending   (boolean — device clears after reading)
│       └── config/           ← remote threshold overrides
│           ├── tempWarningC
│           ├── tempCriticalC
│           ├── humidityWarningPct
│           ├── mlRiskThreshold
│           ├── irisMatchThreshold
│           └── anomalyScoreThreshold
│
├── readings/
│   └── {deviceId}/
│       └── {pushId}/         ← ambient sensor time-series
│           └── (same fields as latest/)
│
├── signins/                  ← biometric authentication events
│   └── {deviceId}/
│       └── {pushId}/
│           ├── userId        (string)
│           ├── userName      (string)
│           ├── deviceId      (string)
│           ├── matchScore    (float — lower is better, 0=identical)
│           ├── success       (boolean)
│           ├── anomalyScore  (float)
│           └── ts            (number — JS millis)
│
├── users/                    ← enrolled user registry
│   └── {userId}/
│       ├── userId            (string)
│       ├── name              (string)
│       ├── deviceId          (string)
│       ├── enrolledAt        (number — JS millis)
│       └── active            (boolean)
│
└── alerts/
    └── {deviceId}/
        └── {pushId}/
            ├── deviceId      (string)
            ├── alertType     (string — "env_threshold"/"brute_force"/"suspicious_score")
            ├── userId        (string — biometric alerts only)
            ├── anomalyScore  (float — biometric alerts)
            ├── riskScore     (float — env alerts)
            ├── ts            (number — JS millis)
            └── acknowledged  (boolean)
```

---

## 6. State Machine Flowchart

```
                       Power on
                          │
                          ▼
                    ┌──────────┐
                    │   INIT   │  hardware init, config load, ML load
                    └────┬─────┘
                         │ OK
                         ▼
                    ┌──────────┐
                    │CONNECTING│  WiFi.begin() + Firebase.begin()
                    └────┬─────┘
                         │ connected (or timeout → offline mode)
                         ▼
                    ┌──────────┐
                    │  READY   │  all modules up, first heartbeat sent
                    └────┬─────┘
                         │ immediate
                         ▼
         ┌──────────────────────────────────────────────┐
         │                                              │
         ▼   env riskScore ≥ threshold                  │
    ┌──────────┐ ─────────────────────────► ┌────────┐  │
    │MONITORING│                            │ ALERT  │  │
    │  (idle)  │ ◄──────────────────────── └────────┘  │
    └────┬─────┘   riskScore < threshold (cooldown)     │
         │                                              │
         │ short btn press  Firebase enroll command     │
         │                         │                   │
         ▼                         ▼                   │
    ┌──────────────┐         ┌──────────┐              │
    │AUTHENTICATING│         │ENROLLING │              │
    └──────┬───────┘         └────┬─────┘              │
           │ pass                 │ done               │
    ┌──────▼──────┐               └───────────────────►│
    │AUTHENTICATED│  timeout/anomaly
    │  relay ON   │ ─────────────────────────────────► back to MONITORING
    └─────────────┘
           │ no match
    ┌──────▼──────┐
    │  REJECTED   │ ──────────────────────────────────► MONITORING
    └─────────────┘

    Any state ─────────────────────────────────────► ┌────────┐
                    sensor fault / 5× failure         │ ERROR  │ ──► restart
                                                      └────────┘
```

**Actuator behaviour by state:**

| State | Relay | LED |
|---|---|---|
| INIT | OFF | Fast blink |
| CONNECTING | OFF | Fast blink |
| READY | OFF | Slow blink |
| MONITORING | OFF | Slow blink |
| ENROLLING | OFF | Fast blink |
| AUTHENTICATING | OFF | Fast blink |
| AUTHENTICATED | ON | Solid |
| REJECTED | OFF | 3 fast flashes |
| ALERT | ON | Fast blink |
| ERROR | OFF | SOS pattern |

---

## 7. Biometric Pipeline

### Iris Feature Extraction
```
OV2640 frame (160×120 grayscale, JPEG decoded)
    ↓
_extractFeatures():
    divide into 8×8 = 64 cells
    mean intensity per cell → float[64] in [0, 1]
    ↓
IrisCapture { features[64], valid, timestampMs }
```

### Enrollment
```
captureAverage(numFrames=5, delayMs=200ms)
    ↓ up to 5 valid frames accumulated, averaged
BiometricManager.enroll(userId, name, IrisCapture)
    ↓ save to /bio/{userId}_t{N}.bin in SPIFFS
    ↓ update /bio/users.json registry
    ↓ FirebaseManager.pushEnrollment() → /users/{userId}
```

### Matching
```
BiometricManager.match(IrisCapture query)
    for each enrolled user:
        for each of up to 5 templates:
            distance = sqrt(Σ(q[i]-t[i])² / 64)   ← normalised RMS
        best_score = min(template distances)
    winner = user with lowest best_score
    if winner.score < irisMatchThreshold (0.30): PASS
    else: FAIL
    → MatchResult { matched, userId, userName, score, templateIdx }
```

### Anomaly Scoring
```
AnomalyDetector.record(MatchResult, success)
    sliding window: last 20 events
    failureRate    = failures / windowSize
    scoreProximity = max(0, 1 - score/threshold) if success, else 0
    frequencySpike = (events in last 60s > 5) ? 1.0 : ratio
    composite = 0.40×failureRate + 0.35×scoreProximity + 0.25×frequencySpike
    → score in [0.0, 1.0]
```

---

## 8. TinyML Pipeline (Ambient)

### Model Architecture
```
Input(3) → Dense(16, ReLU) → Dense(8, ReLU) → Dense(3, Softmax)
~700 parameters, ~4.2 KB TFLite, ~8 KB tensor arena on ESP32
```

### Input Normalisation
```python
temp_norm  = (temp_c - (-10)) / (60 - (-10))   # TEMP_MIN=-10, TEMP_MAX=60
hum_norm   = hum_pct / 100.0
light_norm = light_norm  # already [0,1] from ADC
```

### Training Pipeline
```
mock_data_generator.py → CSV (5000 samples)
model_training.py      → Keras → TFLite → model.tflite
model_converter.py     → bytes → C hex array → tinyml_model.h
ESP32 firmware         → tflite::GetModel() → Invoke()
```

---

## 9. Scaling Strategy

### From 2 to 20 devices
No schema changes needed. Firebase auto-discovers new devices. The dashboard's `onValue("/devices")` listener picks them up. Per-device differentiation via `config.json` in SPIFFS — flash the same binary, change only the config file.

### Database read cost optimisation
For 20+ devices:
1. Use per-device listeners: `db.ref('/devices/' + id).on('value', ...)`
2. Limit sign-in reads: `.limitToLast(50)` on `/signins/{id}`
3. Use Firebase Cloud Functions to aggregate stats server-side

### Biometric template backup
Templates are stored only in device SPIFFS. For fleet management:
- On enrollment, optionally upload the template blob to Firebase Storage
- Device can re-download templates after a SPIFFS format (e.g., after OTA)

### OTA firmware updates
Add `ArduinoOTA` and trigger from Firebase `/devices/{id}/commands/otaUrl`. Templates stored in SPIFFS survive OTA since OTA only rewrites the app partition.

---

## 10. Troubleshooting

See `docs/TROUBLESHOOTING.md` for the full guide.

**Top issues:**
1. CONNECTING loop → wrong WiFi credentials or Firebase URL
2. `[CAM] Init failed` → wrong board (needs AI Thinker ESP32-CAM) or wrong partition scheme (needs "Huge APP")
3. Match score always > 0.4 → poor/inconsistent lighting; re-enroll with IR illumination
4. `[BIO] SPIFFS mount failed` → wrong partition scheme or SPIFFS data not uploaded
5. `[AWS] MQTT connect failed` → wrong endpoint, certificate mismatch, or missing IoT policy
6. Firebase `Permission denied` → using prod.rules without correct auth.uid matching
7. Dashboard demo mode → `FIREBASE_CONFIG` has placeholder values

---

## 11. Future Improvements

| Feature | Effort | Value |
|---|---|---|
| Proper iris segmentation (Daugman's algorithm) | High | Greatly improves FAR/FRR |
| CNN embedding via AWS Rekognition | Medium | Production-grade matching |
| INT8 model quantization for ambient model | Low | 4× smaller, faster TFLite inference |
| OTA firmware updates | Medium | No physical access required |
| Template cloud backup + restore | Medium | Survive SPIFFS format, share across devices |
| Multi-factor auth (iris + PIN) | Medium | Higher security for sensitive areas |
| Liveness detection (blink challenge) | High | Defeats photo replay attacks |
| Per-device anomaly baseline calibration | High | Self-calibrating thresholds from fleet history |
| Firebase Cloud Functions alert aggregation | Medium | Offload logic from devices |
| Grafana + InfluxDB export | Medium | Production time-series analytics |
| PlatformIO migration | Low | Better dependency management than Arduino IDE |
| Battery + deep sleep mode | Medium | Solar/battery outdoor nodes |
