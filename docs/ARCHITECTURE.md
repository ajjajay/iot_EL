# System Architecture

## High-level Diagram

```
┌────────────────────────────────────────────────────────────────────────────┐
│                    Iris Biometric Access Control System                    │
│                                                                            │
│  ┌──────────────────────┐   WiFi / HTTPS   ┌──────────────────────────┐   │
│  │  ESP32-CAM Node #1   │ ───────────────► │                          │   │
│  │  (OV2640 iris + DHT) │ ◄─────────────── │  Firebase Realtime DB    │   │
│  └──────────────────────┘   commands       │                          │   │
│                                            │  /devices/{id}/latest    │   │
│  ┌──────────────────────┐   WiFi / HTTPS   │  /devices/{id}/commands  │   │
│  │  ESP32-CAM Node #2   │ ───────────────► │  /signins/{id}/{pushId}  │   │
│  │  (OV2640 iris + DHT) │ ◄─────────────── │  /users/{userId}         │   │
│  └──────────────────────┘                  │  /alerts/{id}/{pushId}   │   │
│         ... (scalable to N)                └──────────────┬───────────┘   │
│                                                           │  WebSocket    │
│  ┌──────────────────────┐   MQTT / TLS     ┌─────────────▼───────────┐   │
│  │  ESP32-CAM Node (×N) │ ──────────────►  │                         │   │
│  │  biometric events    │ ◄────────────── │  AWS IoT Core            │   │
│  └──────────────────────┘   AI agent ACK  │  MQTT broker             │   │
│                                            │  IoT Rules Engine        │   │
│                                            └─────────────┬───────────┘   │
│                                                          │               │
│                                            ┌─────────────▼───────────┐   │
│                                            │  AWS Lambda              │   │
│                                            │  BiometricAlertAgent     │   │
│                                            │  → Bedrock Agent         │   │
│                                            │  → SNS user notification │   │
│                                            └─────────────────────────┘   │
│                                                                            │
│  ┌────────────────────────────────────────────────────────────────────┐   │
│  │   Web Dashboard (HTML/CSS/JS)                                      │   │
│  │   Sign-in log · Enrolled users · Security alerts · Sensor charts   │   │
│  └────────────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────────┘
```

## ESP32 Internal Architecture

```
                    ┌──────────────────────────────────┐
                    │      esp32_sensor_node.ino        │
                    │      (Main orchestrator)           │
                    └──────┬───────────────────────────┘
                           │ owns
         ┌─────────────────┼──────────────────────────────────────┐
         │                 │                  │                    │
         ▼                 ▼                  ▼                    ▼
  ┌────────────┐   ┌──────────────┐   ┌────────────┐   ┌─────────────────┐
  │  State     │   │  Iris        │   │  Sensor    │   │  Actuator       │
  │  Manager   │   │  Camera      │   │  Manager   │   │  Controller     │
  │  (FSM)     │   │  (OV2640)    │   │  DHT+LDR   │   │  Relay + LED    │
  └──────┬─────┘   └──────┬───────┘   └──────┬─────┘   └────────┬────────┘
         │ state           │ IrisCapture      │ readings          │ relay state
         │                 ▼                  ▼
         │          ┌─────────────┐    ┌────────────┐
         │          │  Biometric  │    │  ML        │
         │          │  Manager    │    │  Inference │
         │          │  (SPIFFS)   │    │  (TFLite)  │
         │          └──────┬──────┘    └──────┬─────┘
         │                 │ MatchResult       │ risk score
         │                 ▼
         │          ┌─────────────┐
         │          │  Anomaly    │
         │          │  Detector   │
         │          │  (sliding   │
         │          │   window)   │
         │          └──────┬──────┘
         │                 │ anomaly score
         │                 ▼
         │          ┌─────────────┐
         │          │  Alert      │
         │          │  Manager    │
         │          └──────┬──────┘
         │                 │
         └─────────────────┼──────────────────────────────┐
                           │                              │
                           ▼                              ▼
                    ┌─────────────────┐          ┌──────────────────┐
                    │  Firebase       │          │  AWS IoT         │
                    │  Manager        │          │  Manager         │
                    │  (+ offline q)  │          │  (MQTT/TLS)      │
                    └─────────────────┘          └──────────────────┘
```

## Finite State Machine

```
              Boot
    ─────────────────► ┌──────────┐
                       │   INIT   │  hardware init, config load, ML load
                       └────┬─────┘
                            │ OK
                            ▼
                       ┌──────────┐
                       │CONNECTING│ ◄───── WiFi lost (from any active state)
                       └────┬─────┘
                            │ WiFi + Firebase connected
                            ▼
                       ┌──────────┐
                       │  READY   │  all modules up, first heartbeat sent
                       └────┬─────┘
                            │ immediate
                            ▼
              ┌─────────────────────────────────────────┐
              │                                         │
              ▼   riskScore ≥ 0.6 (env)                 │
         ┌──────────┐ ─────────────────► ┌────────┐    │
         │MONITORING│                    │ ALERT  │    │
         │          │ ◄───────────────── │        │    │
         └────┬─────┘  riskScore < 0.6   └────────┘    │
              │                                         │
              │ short btn press       Firebase enroll   │
              ▼                       command           │
         ┌──────────────┐        ┌──────────┐           │
         │AUTHENTICATING│        │ENROLLING │           │
         └──────┬───────┘        └────┬─────┘           │
                │ match pass     done │                  │
          ┌─────▼─────┐              │                  │
          │AUTHENTICATED│            └──────────────────┘
          │(relay ON)  │
          └──────┬──────┘
                 │ timeout / anomaly
                 ▼
                 │ no match
         ┌───────▼──────┐
         │   REJECTED   │ ──────────────────────────► MONITORING
         └──────────────┘

    Any state ──────────────────────────────────────► ┌────────┐
                  fault                               │ ERROR  │ ──► restart
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
| REJECTED | OFF | SOS (3 flashes) |
| ALERT | ON | Fast blink |
| ERROR | OFF | SOS pattern |

## Biometric Data Flow — Single Authentication

```
  1. User presses button (GPIO authButtonPin)
  2. FSM: MONITORING → AUTHENTICATING
  3. IrisCamera.capture()
       └── OV2640 grayscale frame (160×120)
       └── _extractFeatures() → 8×8 grid mean intensity → float[64]
  4. BiometricManager.match(iris)
       └── For each enrolled user's template(s):
           └── _rms(query, template) = sqrt(Σ(d²) / 64)
       └── Best match < threshold (0.30) → MatchResult { matched, userId, score }
  5. AnomalyDetector.record(result)
       └── 0.40 × failureRate + 0.35 × scoreProximity + 0.25 × frequencySpike
       └── Returns composite anomaly score [0.0, 1.0]
  6. If matched:
       ActuatorController.setRelay(ON)  — door opens
       FSM → AUTHENTICATED
       After authDisplayMs → relay OFF, FSM → MONITORING
  7. If not matched:
       FSM → REJECTED → MONITORING
  8. FirebaseManager.pushSignIn(userId, userName, score, success, anomalyScore)
       └── Appended to /signins/{deviceId}/
  9. AWSIoTManager.publishBiometricEvent(userId, score, success)
       └── Topic: iot/{thingName}/biometric/signin
  10. If anomalyScore ≥ threshold:
       AlertManager.sendAnomaly(userId, score, "brute_force"|"suspicious_score")
       └── AWSIoTManager.publishAlertJson() → iot/{thingName}/biometric/alert
       └── FirebaseManager.pushBiometricAlert()
       └── AWS IoT Rule → Lambda BiometricAlertAgent → Bedrock Agent → SNS
       └── Lambda publishes ACK to iot/{thingName}/ai/alerts
       └── AWSIoTManager._onMessage() sets _agentAckPending
       └── Main loop calls AlertManager.onAgentAck()
```

## Enrollment Flow

```
  Dashboard             Firebase                ESP32
     │                     │                     │
     │ sendEnrollCommand()  │                     │
     │ ─────────────────►  │                     │
     │                     │  /devices/{id}/      │
     │                     │  commands/enroll/    │
     │                     │  { userId, name,     │
     │                     │    pending: true }   │
     │                     │ ─────────────────►   │
     │                     │                     │ pollEnrollCommand() (5s)
     │                     │                     │ reads + clears pending
     │                     │                     │ FSM → ENROLLING
     │                     │                     │ IrisCamera.captureAverage(5)
     │                     │                     │ BiometricManager.enroll()
     │                     │                     │ saves /bio/{userId}_t0.bin
     │                     │   pushEnrollment()  │
     │                     │ ◄─────────────────── │
     │                     │  /users/{userId}     │
```

## Module Responsibilities

| Module | Single Responsibility |
|---|---|
| `StateManager` | Legal FSM transitions, state logging |
| `ConfigManager` | JSON config load/save from SPIFFS |
| `IrisCamera` | OV2640 capture + 64-element feature extraction |
| `BiometricManager` | SPIFFS template storage, enroll/match/remove |
| `AnomalyDetector` | Sliding-window composite anomaly scoring |
| `AlertManager` | Throttled alert dispatch (AWS MQTT + Firebase) |
| `SensorManager` | DHT22/LDR I/O + EMA smoothing + retry |
| `ActuatorController` | GPIO relay/LED control + override expiry |
| `MLInference` | TFLite env risk inference (secondary) |
| `FirebaseManager` | RTDB push/pull + offline ring buffer + biometric paths |
| `AWSIoTManager` | MQTT/TLS publish + subscribe + agent ACK handling |
| `esp32_sensor_node.ino` | Wire all modules together; no business logic |

## Key Design Decisions

1. **8×8 zonal iris descriptor** — 64 floats extracted by partitioning the grayscale frame into an 8×8 grid and taking mean intensity per cell. Simple, fast, PSRAM-free. Production systems would use Gabor wavelets or CNN embeddings, but this works for proof-of-concept gating.

2. **Normalised RMS distance** — `sqrt(Σ(d²)/64)` gives a scale-invariant distance in [0, 1]. Threshold of 0.30 balances FAR/FRR empirically; adjustable per-device via `irisMatchThreshold` in config.

3. **Multiple templates per user** (up to 5) — Enrolled via `captureAverage(5 frames)`. Ring-buffer overwrite when full. Improves match rate across lighting changes.

4. **Composite anomaly scoring** — Three independent signals (failure rate, score proximity to threshold, frequency spike) are weighted and combined. No single signal causes a false alert; requires corroboration across dimensions.

5. **Dual alert path** — Firebase stores the audit log; AWS IoT routes to Lambda/Bedrock for active user notification. This decouples audit from notification and lets the cloud path be replaced without firmware changes.

6. **Agent ACK round-trip** — Lambda publishes `{ ack: true }` back to `iot/{thing}/ai/alerts`. The device records acknowledgement in SPIFFS to avoid re-sending if it reconnects. Closes the feedback loop between edge and cloud.

7. **Ring buffer offline queue** — Prevents data loss during WiFi outages. 30 readings × ~60 bytes = ~1.8 KB RAM cost. Sign-in events are also buffered separately.

8. **Override expiry on relay** — Dashboard relay commands auto-expire after 5 minutes, preventing permanently open doors if the dashboard crashes.
