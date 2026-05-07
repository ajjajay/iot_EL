# System Architecture

## High-level Diagram

```
  ┌─────────────────────────────────────────────────────────────────────────┐
  │  EDGE LAYER (any combination of node types)                             │
  │                                                                         │
  │  ┌──────────────────────────┐    ┌──────────────────────────────────┐   │
  │  │  ESP32-CAM Node (×N)     │    │  Raspberry Pi Node (×N)          │   │
  │  │  C++ / Arduino           │    │  Python 3                        │   │
  │  │  OV2640 · DHT22 · LDR    │    │  picamera2/OpenCV · DHT22        │   │
  │  │  Firebase ESP Client     │    │  MCP3008 ADC · RPi.GPIO          │   │
  │  │  PubSubClient MQTT/TLS   │    │  requests REST · paho-mqtt       │   │
  │  │  TFLite Micro            │    │  tflite-runtime                  │   │
  │  └──────────┬───────────────┘    └──────────────┬───────────────────┘   │
  └─────────────┼───────────────────────────────────┼─────────────────────-─┘
                │  WiFi / HTTPS                      │  Ethernet / WiFi
  ┌─────────────▼────────────┐      ┌────────────────▼──────────────────────┐
  │  Firebase Realtime DB    │      │  AWS IoT Core                         │
  │  /devices/{id}/latest    │      │  Topics:                              │
  │  /devices/{id}/commands  │      │    iot/{thing}/biometric/signin        │
  │  /signins/{id}/{pushId}  │      │    iot/{thing}/biometric/alert         │
  │  /users/{userId}         │      │    iot/{thing}/ai/alerts (ACK)         │
  │  /alerts/{id}/{pushId}   │      │  IoT Rules Engine                     │
  │  /readings/{id}/{pushId} │      │    → Lambda BiometricAlertAgent        │
  └────────────┬─────────────┘      │    → AWS Bedrock Agent                │
               │  WebSocket         │    → SNS email/SMS notification        │
  ┌────────────▼──────────────────────────────────────────────────────────┐ │
  │  Web Dashboard (HTML / CSS / Chart.js)                                │ │
  │  Sign-in log · Enrolled users · Security alerts · Sensor charts       │ │
  │  Enrollment command panel · Manual door controls · Device health      │ │
  └───────────────────────────────────────────────────────────────────────┘ │
```

---

## Edge Node Internal Architecture

Both node types implement the same module graph. The left column shows the
ESP32 class; the right shows the Python equivalent.

```
                 ┌─────────────────────────────────────┐
                 │  esp32_sensor_node.ino  /  main.py   │
                 │  (Main FSM orchestrator)              │
                 └──────┬──────────────────────────────-┘
                        │ owns
    ┌───────────────────┼───────────────────────────────────────────┐
    │                   │                   │                        │
    ▼                   ▼                   ▼                        ▼
┌──────────┐   ┌──────────────┐   ┌──────────────┐   ┌─────────────────────┐
│ State    │   │ IrisCamera   │   │ Sensor       │   │ Actuator            │
│ Manager  │   │ iris_camera  │   │ Manager      │   │ Controller          │
│ (FSM)    │   │ (OV2640 /    │   │ sensor_mgr   │   │ actuator_ctrl       │
│          │   │  picamera2)  │   │ DHT22 + LDR  │   │ Relay + LED + GPIO  │
└────┬─────┘   └──────┬───────┘   └──────┬───────┘   └─────────────────────┘
     │ state           │ IrisCapture      │ SensorReading
     │                 ▼                  ▼
     │          ┌──────────────┐   ┌──────────────┐
     │          │ Biometric    │   │ ML Inference │
     │          │ Manager      │   │ ml_inference │
     │          │ biometric_mgr│   │ TFLite       │
     │          │ SPIFFS / fs  │   │ (Micro / RT) │
     │          └──────┬───────┘   └──────┬───────┘
     │                 │ MatchResult       │ risk score
     │                 ▼
     │          ┌──────────────┐
     │          │ Anomaly      │
     │          │ Detector     │
     │          │ anomaly_det  │
     │          │ (window=20)  │
     │          └──────┬───────┘
     │                 │ anomaly score
     │                 ▼
     │          ┌──────────────┐
     │          │ Alert        │
     │          │ Manager      │
     │          │ alert_mgr    │
     │          └──────┬───────┘
     │                 │
     └─────────────────┼──────────────────────────────┐
                       │                              │
                       ▼                              ▼
              ┌──────────────────┐         ┌──────────────────────┐
              │ Firebase         │         │ AWS IoT              │
              │ Manager          │         │ Manager              │
              │ firebase_mgr     │         │ aws_iot_mgr          │
              │ (+ offline queue)│         │ MQTT/TLS paho        │
              └──────────────────┘         └──────────────────────┘
```

---

## Finite State Machine

10 states, enforced legal transition table. Illegal transitions are rejected and logged.

```
              Boot
   ──────────────────► ┌──────────┐
                       │   INIT   │  hardware init, config load, ML model load
                       └────┬─────┘
                            │ OK
                            ▼
                       ┌──────────┐
                       │CONNECTING│ ◄─── WiFi lost (from MONITORING or ALERT)
                       └────┬─────┘
                            │ Firebase connected
                            ▼
                       ┌──────────┐
                       │  READY   │  all modules up, first heartbeat sent
                       └────┬─────┘
                            │ immediate
                            ▼
             ┌─────────────────────────────────────────┐
             │                                         │
             ▼   env riskScore ≥ mlRiskThreshold        │
        ┌──────────┐ ─────────────────► ┌────────┐     │
        │MONITORING│                    │ ALERT  │     │
        │  (idle)  │ ◄───────────────── │(10 s)  │     │
        └────┬─────┘  score < threshold  └────────┘     │
             │                                           │
             │ short button press    Firebase enroll cmd │
             ▼                              │            │
        ┌──────────────┐         ┌──────────┐           │
        │AUTHENTICATING│         │ENROLLING │           │
        └──────┬───────┘         └────┬─────┘           │
               │ match pass           │ done             │
         ┌─────▼──────┐               └────────────────► │
         │AUTHENTICATED│ door open
         │ relay ON    │ ─────── authDisplayMs timeout ──► MONITORING
         └─────────────┘
               │ no match
         ┌─────▼──────┐
         │  REJECTED   │ ──── authDisplayMs timeout ────► MONITORING
         └─────────────┘

   Any state ─────────────────────────────────────────► ┌────────┐
                  sensor fault / camera fail             │ ERROR  │ ──► restart
                                                         └────────┘
```

**Actuator behaviour by state:**

| State | Relay | LED |
|---|---|---|
| INIT | OFF | Fast blink (5 Hz) |
| CONNECTING | OFF | Fast blink (5 Hz) |
| READY | OFF | Slow blink (1 Hz) |
| MONITORING | OFF | Slow blink (1 Hz) |
| ENROLLING | OFF | Fast blink (5 Hz) |
| AUTHENTICATING | OFF | Fast blink (5 Hz) |
| AUTHENTICATED | **ON** | Solid |
| REJECTED | OFF | 3 fast flashes |
| ALERT | **ON** | Fast blink (5 Hz) |
| ERROR | OFF | SOS morse pattern |

---

## Biometric Pipeline — Single Authentication

```
1. User presses button (GPIO authButtonPin)
2. FSM: MONITORING → AUTHENTICATING

3. IrisCamera.capture() / iris_camera.capture()
     └── grayscale frame (320×240)
     └── _extract_features():
           divide into 8×8 = 64 cells
           mean pixel intensity per cell → float32[64] in [0,1]

4. BiometricManager.match(iris, threshold)
     └── for each enrolled user, for each of ≤5 templates:
           distance = sqrt( Σ(q[i]-t[i])² / 64 )   ← normalised RMS
     └── winner = user with lowest distance
     └── if winner.distance < irisMatchThreshold (0.30) → PASS
     └── else → FAIL

5. AnomalyDetector.record(result, success)
     └── sliding window: last 20 events
     └── failureRate    = consecutive_fails / BRUTE_FORCE_LIMIT
     └── scoreProximity = ramp from 0→1 as distance approaches threshold
     └── frequencySpike = > 5 events in 60 s → 1.0
     └── composite = 0.40×failRate + 0.35×prox + 0.25×freq  → [0.0, 1.0]

6a. If matched:
      relay → ON (door opens)
      FSM → AUTHENTICATED
      After authDisplayMs → relay → OFF, FSM → MONITORING
      If anomalyScore ≥ anomalyScoreThreshold → AlertManager.send_anomaly()

6b. If not matched:
      FSM → REJECTED → MONITORING (after authDisplayMs)
      If bruteForceScore ≥ threshold → AlertManager.send_anomaly("brute_force")

7. FirebaseManager.push_sign_in() → /signins/{deviceId}/{pushId}
8. AWSIoTManager.publish_biometric_event() → iot/{thing}/biometric/signin
9. AlertManager.send_anomaly() (if triggered):
     → AWS IoT iot/{thing}/biometric/alert
     → IoT Rule → Lambda BiometricAlertAgent
     → Bedrock Agent → SNS email/SMS
     → Lambda publishes ACK to iot/{thing}/ai/alerts
     → Device receives ACK via MQTT subscription
```

---

## Enrollment Flow

```
  Dashboard              Firebase                 Device
     │                      │                       │
     │ sendEnrollCommand()   │                       │
     │ ─────────────────►   │                       │
     │                      │  /devices/{id}/       │
     │                      │  commands/enroll/     │
     │                      │  { userId, name,      │
     │                      │    pending: true }    │
     │                      │ ──────────────────►   │
     │                      │                       │ poll_enroll_command() (5 s)
     │                      │                       │ reads + clears pending flag
     │                      │                       │ FSM → ENROLLING
     │                      │                       │ capture_average(5 frames)
     │                      │                       │ biometric.enroll()
     │                      │                       │ saves bio/{userId}_t0.npy
     │                      │   push_enrollment()   │
     │                      │ ◄──────────────────── │
     │                      │  /users/{userId}      │
```

---

## Module Responsibilities

| Module | Single Responsibility | ESP32 | RPi |
|---|---|---|---|
| `StateManager` / `state_manager` | Legal FSM transitions, elapsed-time logging | ✓ | ✓ |
| `ConfigManager` / `config_manager` | JSON config load from SPIFFS / filesystem | ✓ | ✓ |
| `IrisCamera` / `iris_camera` | Camera capture + 64-element feature extraction | OV2640 | picamera2 / OpenCV |
| `BiometricManager` / `biometric_manager` | Template storage, enroll/match/remove | SPIFFS `.bin` | Filesystem `.npy` |
| `AnomalyDetector` / `anomaly_detector` | Sliding-window composite scoring | ✓ | ✓ |
| `AlertManager` / `alert_manager` | Throttled dual-path alert dispatch | ✓ | ✓ |
| `SensorManager` / `sensor_manager` | DHT22 + LDR I/O, EMA smoothing, retry | 12-bit ADC | MCP3008 SPI |
| `ActuatorController` / `actuator_controller` | GPIO relay/LED + override expiry | `digitalWrite` | `RPi.GPIO` |
| `MLInference` / `ml_inference` | Ambient env risk inference | TFLite Micro | tflite-runtime |
| `FirebaseManager` / `firebase_manager` | RTDB push/pull + offline ring buffer | ESP client lib | REST API |
| `AWSIoTManager` / `aws_iot_manager` | MQTT/TLS publish + subscribe + agent ACK | PubSubClient | paho-mqtt |
| `.ino` / `main.py` | Wire all modules; no business logic | ✓ | ✓ |

---

## Key Design Decisions

1. **8×8 zonal iris descriptor** — 64 floats extracted by partitioning the grayscale frame into an 8×8 grid and taking mean intensity per cell. Zero external dependencies, fits in on-chip SRAM/RAM, no segmentation required. Production systems would use Gabor wavelets or CNN embeddings.

2. **Normalised RMS distance** — `sqrt(Σ(d²)/64)` is scale-invariant and maps to [0, 1]. The threshold of 0.30 is adjustable per-device via `irisMatchThreshold` in config.

3. **Multiple templates per user** (≤5) — `capture_average(5 frames)` enrolled per template. Ring-buffer overwrite when full. Improves match stability across lighting variation.

4. **Composite anomaly scoring** — Three independent signals (failure rate, score proximity, frequency) are weighted to require corroboration before alerting. No single signal triggers a false alert.

5. **Dual alert path** — Firebase stores the audit log; AWS IoT routes to Lambda/Bedrock for active user notification. Decouples audit from notification; cloud path is replaceable without firmware changes.

6. **Agent ACK round-trip** — Lambda publishes `{ ack: true }` back to `iot/{thing}/ai/alerts`. Device receives via MQTT subscription, extends cooldown to prevent re-alert. Closes the feedback loop.

7. **Offline ring buffer** — 30-entry queue prevents data loss during network outages. Flushed on reconnect. Cost: ~1.8 KB RAM on ESP32.

8. **5-minute relay override expiry** — Dashboard relay commands auto-expire, preventing permanently open doors if dashboard connectivity is lost.

9. **Same config JSON schema for both nodes** — A single `config.json` format covers both ESP32 and RPi (key names are identical). Only RPi adds `ldrSpiChannel`, `tfliteModelPath`, and certificate path fields.
