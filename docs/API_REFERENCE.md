# API Reference

## Firebase Realtime Database Paths

### Device State

| Path | Type | Description |
|---|---|---|
| `/devices/{id}/meta/deviceId` | string | Device identifier |
| `/devices/{id}/meta/location` | string | Physical location label |
| `/devices/{id}/meta/firmware` | string | Firmware version string |
| `/devices/{id}/meta/registeredAt` | number | Unix timestamp (seconds) of first boot |
| `/devices/{id}/online` | boolean | Current connectivity status |

### Latest Ambient Reading (updated every `sensorIntervalMs`)

| Path | Type | Range | Description |
|---|---|---|---|
| `/devices/{id}/latest/temperatureC` | float | -40 – 80 | Temperature in °C |
| `/devices/{id}/latest/humidityPct` | float | 0 – 100 | Relative humidity % |
| `/devices/{id}/latest/lightRaw` | int | 0 – 4095 | Raw 12-bit ADC value |
| `/devices/{id}/latest/lightNorm` | float | 0.0 – 1.0 | Normalised light level |
| `/devices/{id}/latest/riskScore` | float | 0.0 – 1.0 | ML composite env risk score |
| `/devices/{id}/latest/mlLabel` | int | 0/1/2 | 0=normal, 1=warning, 2=critical |
| `/devices/{id}/latest/pNormal` | float | 0.0 – 1.0 | Softmax probability — normal |
| `/devices/{id}/latest/pWarning` | float | 0.0 – 1.0 | Softmax probability — warning |
| `/devices/{id}/latest/pCritical` | float | 0.0 – 1.0 | Softmax probability — critical |
| `/devices/{id}/latest/state` | string | FSM state | Current FSM state name |
| `/devices/{id}/latest/ts` | number | ms | JavaScript-style timestamp (millis since epoch) |

### Heartbeat (updated every `heartbeatIntervalMs`)

| Path | Type | Description |
|---|---|---|
| `/devices/{id}/heartbeat/ts` | number | Unix seconds |
| `/devices/{id}/heartbeat/state` | string | FSM state at heartbeat time |
| `/devices/{id}/heartbeat/heapFree` | number | Free heap bytes |
| `/devices/{id}/heartbeat/uptime` | number | Seconds since boot |

### Commands (written by dashboard, polled by device)

| Path | Type | Values | Description |
|---|---|---|---|
| `/devices/{id}/commands/relayOverride` | string | `"ON"` / `"OFF"` / `"AUTO"` | Manual relay control |
| `/devices/{id}/commands/enroll/userId` | string | any | User ID to enroll |
| `/devices/{id}/commands/enroll/name` | string | any | Full name |
| `/devices/{id}/commands/enroll/pending` | boolean | `true` | Set true to trigger enrollment; device clears it after reading |

### Remote Config (written by admin, read by device at boot)

| Path | Type | Description |
|---|---|---|
| `/devices/{id}/config/tempWarningC` | float | Override warning threshold |
| `/devices/{id}/config/tempCriticalC` | float | Override critical threshold |
| `/devices/{id}/config/humidityWarningPct` | float | Override humidity threshold |
| `/devices/{id}/config/mlRiskThreshold` | float | Override ML risk threshold |
| `/devices/{id}/config/sensorIntervalMs` | int | Override polling interval |
| `/devices/{id}/config/irisMatchThreshold` | float | Override iris match distance threshold |
| `/devices/{id}/config/anomalyScoreThreshold` | float | Override anomaly alert threshold |

### Time-series Ambient Readings

| Path | Type | Description |
|---|---|---|
| `/readings/{id}/{pushId}` | object | Full reading snapshot (same fields as `/latest`) |

### Sign-in Log (biometric authentication events)

| Path | Type | Description |
|---|---|---|
| `/signins/{id}/{pushId}/userId` | string | Matched (or attempted) user ID |
| `/signins/{id}/{pushId}/userName` | string | User's full name |
| `/signins/{id}/{pushId}/deviceId` | string | Source device |
| `/signins/{id}/{pushId}/matchScore` | float | Iris match distance (lower = better) |
| `/signins/{id}/{pushId}/success` | boolean | Authentication result |
| `/signins/{id}/{pushId}/anomalyScore` | float | Composite anomaly score at event time |
| `/signins/{id}/{pushId}/ts` | number | JS timestamp (ms) |

> Sign-in records are write-once per device (enforced by prod.rules).
> Use `.limitToLast(50)` to fetch the 50 most recent events.

### Enrolled Users

| Path | Type | Description |
|---|---|---|
| `/users/{userId}/userId` | string | User identifier |
| `/users/{userId}/name` | string | Full display name |
| `/users/{userId}/deviceId` | string | Device where templates are stored |
| `/users/{userId}/enrolledAt` | number | JS timestamp (ms) of enrollment |
| `/users/{userId}/active` | boolean | Whether the user is active |

### Alerts

| Path | Type | Description |
|---|---|---|
| `/alerts/{id}/{pushId}/deviceId` | string | Source device |
| `/alerts/{id}/{pushId}/alertType` | string | `"env_threshold"` / `"brute_force"` / `"suspicious_score"` |
| `/alerts/{id}/{pushId}/userId` | string | Affected user (biometric alerts only) |
| `/alerts/{id}/{pushId}/anomalyScore` | float | Anomaly score at trigger (biometric alerts) |
| `/alerts/{id}/{pushId}/riskScore` | float | Risk score at trigger (env alerts) |
| `/alerts/{id}/{pushId}/ts` | number | JS timestamp (ms) |
| `/alerts/{id}/{pushId}/acknowledged` | boolean | Set true by dashboard |

---

## C++ Module API

### StateManager

```cpp
StateManager fsm;
fsm.current()               // → DeviceState enum
fsm.transition(next)        // → bool (false if illegal transition)
fsm.timeInState()           // → unsigned long ms in current state
fsm.transitionCount()       // → uint32_t total transitions

stateToString(state)        // → const char* e.g. "MONITORING", "AUTHENTICATING"
```

**DeviceState values**: `INIT`, `CONNECTING`, `READY`, `MONITORING`, `ALERT`, `ERROR`, `ENROLLING`, `AUTHENTICATING`, `AUTHENTICATED`, `REJECTED`

### IrisCamera

```cpp
IrisCamera cam;             // uses AI Thinker default pins
cam.begin()                 // → bool; initialises OV2640 + tunes sensor
IrisCapture c = cam.capture();
// c.features[64]  float32 descriptor
// c.valid         bool
// c.timestampMs   millis since boot

IrisCapture c = cam.captureAverage(numFrames=5, delayMs=200);
// averages N valid frames; returns invalid if < 2 valid frames captured
cam.isReady()               // → bool
```

### BiometricManager

```cpp
BiometricManager bio;
bio.begin()                        // → bool; mounts SPIFFS, loads templates
bio.enroll(userId, name, iris)     // → bool; saves template to SPIFFS
MatchResult r = bio.match(iris);
// r.matched, r.userId[32], r.userName[64], r.score, r.templateIdx
bio.removeUser(userId)             // → bool; deletes all templates + registry entry
bio.saveRegistry()                 // → bool; writes /bio/users.json to SPIFFS
bio.userCount()                    // → uint8_t
```

**Constants**: `BIO_MAX_USERS=16`, `BIO_MAX_TEMPLATES=5`, `BIO_MATCH_THRESH=0.30f`

### AnomalyDetector

```cpp
AnomalyDetector anomaly(matchThreshold=BIO_MATCH_THRESH);
float score = anomaly.record(matchResult, success);
// score = 0.40×failureRate + 0.35×scoreProximity + 0.25×frequencySpike
// returns composite anomaly score in [0.0, 1.0]

anomaly.bruteForceScore()          // → float (failure-rate component only)
anomaly.consecutiveFailures()      // → uint8_t
```

**Window**: last 20 events; brute-force threshold: 3+ consecutive failures.

### AlertManager

```cpp
AlertManager alerts(thingName, awsIoTPtr, firebasePtr);
// awsIoTPtr may be nullptr (Firebase-only mode)

alerts.sendAnomaly(userId, anomalyScore, alertType, detail="")
// → bool; suppressed if same alertType sent within ALERT_COOLDOWN_MS (30s)
// sends to AWS MQTT topic iot/{thing}/biometric/alert
// appends to Firebase /alerts/{deviceId}/

alerts.onAgentAck(userId, alertType)
// call when AWSIoTManager.getAgentAck() returns true
```

### FirebaseManager

```cpp
FirebaseManager fb(apiKey, dbUrl, email, password, deviceId);
fb.begin()                              // → bool; authenticates + registers
fb.pushReading(reading, ml, state)     // push or enqueue if offline
fb.pollCommands(outRelay)              // → bool if relay command changed
fb.sendHeartbeat(state)
fb.flushQueue()                        // drain offline ring buffer
fb.setOnline(bool)
fb.isConnected()                       // → bool

// Biometric methods
fb.pushSignIn(userId, userName, matchScore, success, anomalyScore)
fb.pushEnrollment(userId, name)
fb.pushBiometricAlert(userId, alertType, anomalyScore)
fb.pollEnrollCommand(outUserId, 32, outName, 64)  // → bool if pending command
```

### AWSIoTManager

```cpp
AWSIoTManager awsIoT(endpoint, thingName, pubSubClient);
awsIoT.begin()                                // → bool; connects + subscribes
awsIoT.loop()                                 // call in loop() — services MQTT
awsIoT.isConnected()                          // → bool

awsIoT.publishBiometricEvent(userId, score, success)
// publishes to: iot/{thingName}/biometric/signin

awsIoT.publishAlertJson(jsonString)           // → bool
// publishes to: iot/{thingName}/biometric/alert

awsIoT.getAgentAck(outUserId, outAlertType)   // → bool; clears pending flag
// true if Bedrock Agent ACK received on iot/{thingName}/ai/alerts
```

**Subscribed MQTT topics**:
- `iot/{thingName}/commands`
- `$aws/things/{thingName}/shadow/update/delta`
- `iot/{thingName}/ai/alerts`

### SensorManager

```cpp
SensorManager sensors(dhtPin, dhtType, ldrPin, emaAlpha=0.3f);
sensors.begin();
SensorReading r = sensors.readNow();
// r.temperatureC, r.humidityPct, r.lightRaw, r.lightNorm, r.valid, r.timestampMs
sensors.failCount()         // consecutive failures since last success
```

### ActuatorController

```cpp
ActuatorController act(relayPin, ledPin, activeLow=true);
act.begin();
act.setRelay(RelayState::ON / ::OFF);
act.setRelayOverride(state, timeoutMs);  // dashboard override
act.clearRelayOverride();
act.hasRelayOverride()      // bool
act.setLedPattern(LedPattern::BLINK_SLOW / ::BLINK_FAST / ::BLINK_SOS / ...);
act.tick();                 // call in loop() — services blink patterns
```

### MLInference

```cpp
MLInference ml;
ml.begin()               // → bool; loads model from tinyml_model.h
MLResult r = ml.infer(tempC, humPct, lightNorm);
// r.pNormal, r.pWarning, r.pCritical, r.riskScore, r.label, r.valid
```

### ConfigManager

```cpp
ConfigManager config;
config.load()             // loads /config.json from SPIFFS
config.save()             // writes current config back to SPIFFS
const DeviceConfig& c = config.cfg();
config.setThresholds(tempWarn, tempCrit, humWarn, lightLow);
```

**Key `DeviceConfig` fields (biometric)**:

| Field | Default | Description |
|---|---|---|
| `authButtonPin` | 15 | GPIO for auth button |
| `irisMatchThreshold` | 0.30 | RMS distance threshold |
| `irisEnrollFrames` | 5 | Frames averaged per enrollment |
| `authDisplayMs` | 3000 | ms relay stays open after auth |
| `anomalyScoreThreshold` | 0.60 | Threshold to trigger alert |
| `alertCooldownMs` | 30000 | ms between duplicate alerts |

---

## AWS IoT MQTT Topics

| Topic | Direction | Description |
|---|---|---|
| `iot/{thing}/biometric/signin` | Device → Cloud | Authentication event telemetry |
| `iot/{thing}/biometric/alert` | Device → Cloud | Anomaly alert payload |
| `iot/{thing}/ai/alerts` | Cloud → Device | Bedrock Agent ACK |
| `iot/{thing}/commands` | Cloud → Device | Remote commands |
| `$aws/things/{thing}/shadow/update/delta` | Cloud → Device | Shadow config updates |

**Sign-in event payload** (`iot/{thing}/biometric/signin`):
```json
{
  "deviceId": "esp32_node_01",
  "userId": "john_doe",
  "matchScore": 0.143,
  "success": true,
  "ts": 1713960305000
}
```

**Alert payload** (`iot/{thing}/biometric/alert`):
```json
{
  "thingName": "esp32_node_01",
  "userId": "unknown",
  "alertType": "brute_force",
  "detail": "3 consecutive failures",
  "anomalyScore": 0.82,
  "ts": 1713960400000
}
```

---

## Ambient ML Model API

### Input tensor

Shape: `[1, 3]` float32

| Index | Feature | Normalisation |
|---|---|---|
| 0 | temperature_c | `(t - (-10)) / (60 - (-10))` → [0, 1] |
| 1 | humidity_pct | `h / 100.0` → [0, 1] |
| 2 | light_norm | already [0, 1] from ADC |

### Output tensor

Shape: `[1, 3]` float32 (softmax)

| Index | Class | Meaning |
|---|---|---|
| 0 | normal | System within safe parameters |
| 1 | warning | Elevated risk; monitor closely |
| 2 | critical | Immediate action required |

**Env risk score** = `output[1] × 0.5 + output[2] × 1.0` ∈ [0, 1]
