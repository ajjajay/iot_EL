# System Architecture

## High-level Diagram

```
┌────────────────────────────────────────────────────────────────────────────┐
│                         IoT Smart Monitor                                  │
│                                                                            │
│  ┌─────────────┐    WiFi / HTTPS    ┌───────────────────────────────────┐  │
│  │  ESP32 #1   │ ─────────────────► │                                   │  │
│  │  Sensor Node│ ◄───────────────── │   Firebase Realtime Database      │  │
│  └─────────────┘    commands        │                                   │  │
│                                     │  /devices/{id}/latest             │  │
│  ┌─────────────┐    WiFi / HTTPS    │  /devices/{id}/heartbeat          │  │
│  │  ESP32 #2   │ ─────────────────► │  /devices/{id}/commands           │  │
│  │  Sensor Node│ ◄───────────────── │  /readings/{id}/{pushId}          │  │
│  └─────────────┘                    │  /alerts/{id}/{pushId}            │  │
│        ...                          └───────────────────┬───────────────┘  │
│  (scalable to N)                                        │  WebSocket        │
│                                                         ▼                  │
│                                     ┌───────────────────────────────────┐  │
│                                     │   Web Dashboard (HTML/CSS/JS)     │  │
│                                     │   Real-time charts, device cards  │  │
│                                     │   Alert timeline, manual controls │  │
│                                     └───────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────────┘
```

## ESP32 Internal Architecture

```
                    ┌──────────────────────────────────┐
                    │         esp32_sensor_node.ino     │
                    │         (Main orchestrator)        │
                    └──────────┬───────────────────────┘
                               │ owns
         ┌─────────────────────┼─────────────────────────┐
         │                     │                          │
         ▼                     ▼                          ▼
  ┌────────────┐       ┌──────────────┐          ┌───────────────┐
  │  State     │       │  Sensor      │          │  Actuator     │
  │  Manager   │       │  Manager     │          │  Controller   │
  │  (FSM)     │       │  DHT22 + LDR │          │  Relay + LED  │
  └──────┬─────┘       └──────┬───────┘          └───────┬───────┘
         │                    │ readings                  │
         │ state              ▼                           │
         │             ┌────────────┐                     │
         │             │  ML        │                     │
         │             │  Inference │                     │
         │             │  TFLite    │                     │
         │             └──────┬─────┘                     │
         │                    │ risk score                │
         └────────────────────┼───────────────────────────┘
                              │
                              ▼
                    ┌─────────────────┐
                    │  Firebase       │
                    │  Manager        │
                    │  (+ offline q)  │
                    └─────────────────┘
```

## Finite State Machine

```
                    ┌──────────┐
              Boot  │          │
        ──────────► │   INIT   │
                    │          │
                    └────┬─────┘
                         │ hardware init OK
                         ▼
                    ┌──────────┐
                    │CONNECTING│ ◄──── WiFi lost (from MONITORING/ALERT)
                    │          │
                    └────┬─────┘
                         │ WiFi + Firebase connected
                         ▼
                    ┌──────────┐
                    │  READY   │
                    │          │
                    └────┬─────┘
                         │ immediate (transient state)
                         ▼
                    ┌──────────┐      risk ≥ threshold
                    │MONITORING│ ─────────────────────► ┌────────┐
                    │          │                         │ ALERT  │
                    │          │ ◄───────────────────── │        │
                    └──────────┘      risk < threshold   └────────┘

     Any state ──────────────────────────────────────► ┌────────┐
                         fault                          │ ERROR  │ ──► restart
                                                        └────────┘
```

## Data Flow — Single Cycle

```
  1. millis() > lastSensorMs + interval
  2. SensorManager.readNow()
       └── DHT22.readTemperature() / readHumidity() + analogRead(LDR)
       └── EMA smoothing
       └── Plausibility check (NaN, out-of-range)
       └── Retry up to 3× on failure
  3. MLInference.infer(temp, hum, light)
       └── Normalise inputs to [0,1]
       └── TFLite Micro → Invoke()
       └── Softmax output [p_normal, p_warning, p_critical]
       └── risk_score = p_warning×0.5 + p_critical×1.0
  4. StateManager.transition() if risk crosses threshold
  5. ActuatorController.setRelay() based on new state
  6. FirebaseManager.pushReading() → /devices/{id}/latest
       └── If offline → enqueue to ring buffer (OFFLINE_QUEUE_SIZE=30)
  7. FirebaseManager.pollCommands() → check /commands/relayOverride
  8. FirebaseManager.sendHeartbeat() if interval elapsed
  9. ActuatorController.tick() → service LED blink patterns
```

## Module Responsibilities

| Module | Single Responsibility |
|---|---|
| `StateManager` | Legal FSM transitions, state logging |
| `ConfigManager` | JSON config load/save from SPIFFS |
| `SensorManager` | Sensor I/O + EMA + retry |
| `ActuatorController` | GPIO control + override expiry + LED patterns |
| `MLInference` | TFLite model lifecycle + inference |
| `FirebaseManager` | RTDB push/pull + offline ring buffer |
| `esp32_sensor_node.ino` | Wire all modules together; no business logic |

## Key Design Decisions

1. **Ring buffer offline queue** — Prevents data loss during WiFi outages. 30 readings × ~60 bytes = ~1.8 KB RAM cost. Acceptable trade-off.

2. **EMA smoothing on sensors** — Eliminates single-sample noise spikes without introducing the lag of a simple moving average. α=0.3 empirically chosen.

3. **Active-low relay inversion** — Most relay breakout boards are active-low. The `relayActiveLow` flag handles both types without code changes.

4. **Override expiry** — Dashboard relay commands auto-expire after 5 minutes. Prevents permanently stuck actuators if the dashboard crashes or loses connectivity.

5. **TFLite ops allowlist** — Only required ops are registered (`AddFullyConnected`, `AddRelu`, etc.). This reduces flash footprint vs. pulling in all TFLite ops.

6. **Flat config JSON** — A single JSON file in SPIFFS covers all device parameters. Avoids the complexity of a NVS key-value scheme while remaining easily editable.
