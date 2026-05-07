# API Reference

## Firebase Realtime Database Paths

All paths are identical regardless of whether the device is an ESP32 or Raspberry Pi.

### Device State

| Path | Type | Description |
|---|---|---|
| `/devices/{id}/meta/deviceId` | string | Device identifier |
| `/devices/{id}/meta/location` | string | Physical location label |
| `/devices/{id}/meta/firmware` | string | Firmware/software version |
| `/devices/{id}/meta/registeredAt` | number | Unix timestamp (seconds) of first boot |
| `/devices/{id}/online` | boolean | Current connectivity status |

### Latest Ambient Reading

Updated every `sensorIntervalMs` milliseconds.

| Path | Type | Range | Description |
|---|---|---|---|
| `/devices/{id}/latest/temperatureC` | float | -40 – 80 | Temperature °C |
| `/devices/{id}/latest/humidityPct` | float | 0 – 100 | Relative humidity % |
| `/devices/{id}/latest/lightRaw` | int | 0 – 4095 | Raw 12-bit ADC value |
| `/devices/{id}/latest/lightNorm` | float | 0.0 – 1.0 | Normalised light level |
| `/devices/{id}/latest/riskScore` | float | 0.0 – 1.0 | ML composite env risk score |
| `/devices/{id}/latest/mlLabel` | int | 0 / 1 / 2 | 0=normal, 1=warning, 2=critical |
| `/devices/{id}/latest/state` | string | — | Current FSM state name |
| `/devices/{id}/latest/ts` | number | ms | JS-style timestamp (ms since epoch) |

### Heartbeat

Updated every `heartbeatIntervalMs` milliseconds.

| Path | Type | Description |
|---|---|---|
| `/devices/{id}/heartbeat/ts` | number | Unix seconds |
| `/devices/{id}/heartbeat/state` | string | FSM state at heartbeat time |
| `/devices/{id}/heartbeat/uptime` | number | Seconds since process start |

### Commands (written by dashboard, polled by device)

| Path | Type | Values | Description |
|---|---|---|---|
| `/devices/{id}/commands/relayOverride` | string | `"ON"` / `"OFF"` / `"AUTO"` | Manual relay control (expires 5 min) |
| `/devices/{id}/commands/enroll/userId` | string | any | User ID to enroll |
| `/devices/{id}/commands/enroll/name` | string | any | Full name |
| `/devices/{id}/commands/enroll/pending` | boolean | `true` | Set true to trigger; device clears after reading |

### Remote Config (optional threshold overrides)

| Path | Type | Description |
|---|---|---|
| `/devices/{id}/config/irisMatchThreshold` | float | Override iris RMS distance threshold |
| `/devices/{id}/config/anomalyScoreThreshold` | float | Override anomaly alert threshold |
| `/devices/{id}/config/mlRiskThreshold` | float | Override ML env risk threshold |
| `/devices/{id}/config/tempWarningC` | float | Override temperature warning threshold |
| `/devices/{id}/config/tempCriticalC` | float | Override critical threshold |
| `/devices/{id}/config/humidityWarningPct` | float | Override humidity threshold |

### Time-series Ambient Readings

| Path | Type | Description |
|---|---|---|
| `/readings/{id}/{pushId}` | object | Full reading snapshot (same fields as `/latest`) |

### Sign-in Log

| Path | Type | Description |
|---|---|---|
| `/signins/{id}/{pushId}/userId` | string | Matched user ID (or `"unknown"`) |
| `/signins/{id}/{pushId}/userName` | string | User display name |
| `/signins/{id}/{pushId}/deviceId` | string | Source device |
| `/signins/{id}/{pushId}/matchScore` | float | Iris RMS distance (lower = better) |
| `/signins/{id}/{pushId}/success` | boolean | Authentication result |
| `/signins/{id}/{pushId}/anomalyScore` | float | Composite anomaly score at event time |
| `/signins/{id}/{pushId}/ts` | number | JS timestamp (ms) |

Use `.limitToLast(50)` to fetch the most recent 50 events efficiently.

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
| `/alerts/{id}/{pushId}/alertType` | string | `"brute_force"` / `"suspicious_signin"` / `"env_threshold"` |
| `/alerts/{id}/{pushId}/userId` | string | Affected user |
| `/alerts/{id}/{pushId}/anomalyScore` | float | Score at trigger time |
| `/alerts/{id}/{pushId}/ts` | number | JS timestamp (ms) |
| `/alerts/{id}/{pushId}/acknowledged` | boolean | Set true by dashboard |

---

## Module API — ESP32 (C++)

### StateManager

```cpp
StateManager fsm;
fsm.current()                   // → DeviceState enum
fsm.transition(newState)        // → bool (false if illegal)
fsm.timeInState()               // → unsigned long ms
```

**DeviceState**: `INIT`, `CONNECTING`, `READY`, `MONITORING`, `ALERT`, `ERROR`, `ENROLLING`, `AUTHENTICATING`, `AUTHENTICATED`, `REJECTED`

### IrisCamera

```cpp
IrisCamera cam;                 // AI Thinker default pins
cam.begin()                     // → bool; inits OV2640
IrisCapture c = cam.capture();
// c.features[64], c.valid, c.timestampMs
IrisCapture c = cam.captureAverage(numFrames=5, delayMs=200);
```

### BiometricManager

```cpp
BiometricManager bio;
bio.begin()                         // mounts SPIFFS, loads templates
bio.enroll(userId, name, iris)      // → bool; saves to /bio/{userId}_t{N}.bin
MatchResult r = bio.match(iris, threshold);
// r.matched, r.userId, r.userName, r.score, r.templateIdx
bio.removeUser(userId)              // → bool
bio.userCount()                     // → uint8_t
```

### AnomalyDetector

```cpp
AnomalyDetector anomaly(matchThreshold);
float score = anomaly.record(matchResult, success);
// 0.40×failureRate + 0.35×scoreProximity + 0.25×frequencySpike → [0.0, 1.0]
anomaly.bruteForceScore()           // → float (failure rate only)
anomaly.consecutiveFailures()       // → uint8_t
```

### AlertManager

```cpp
AlertManager alerts(thingName, awsIoTPtr, firebasePtr);
alerts.sendAnomaly(userId, score, alertType, detail="")
// → bool; suppressed within ALERT_COOLDOWN_MS (30 s) for same alertType
alerts.onAgentAck(userId, alertType)
```

### FirebaseManager

```cpp
FirebaseManager fb(apiKey, dbUrl, email, password, deviceId);
fb.begin()                                          // → bool
fb.pushReading(reading, ml, state)                 // enqueues if offline
fb.pollCommands(outRelay)                          // → bool if changed
fb.sendHeartbeat(state)
fb.flushQueue()
fb.pushSignIn(userId, userName, score, success, anomalyScore)
fb.pushEnrollment(userId, name)
fb.pushBiometricAlert(userId, alertType, anomalyScore)
fb.pollEnrollCommand(outUserId, 32, outName, 64)   // → bool if pending
```

### AWSIoTManager

```cpp
AWSIoTManager aws(endpoint, thingName, rootCA, cert, key);
aws.begin()                                        // → bool
aws.loop()                                         // call in loop()
aws.publishBiometricEvent(userId, score, success)
aws.publishAlertJson(jsonString)                   // → bool
aws.getAgentAck(outUserId, outAlertType)           // → bool (clears flag)
aws.getRelayCommand(outRelay)                      // → bool
```

### SensorManager

```cpp
SensorManager sensors(dhtPin, dhtType, ldrPin, emaAlpha=0.3f);
sensors.begin();
SensorReading r = sensors.readNow();
// r.temperatureC, r.humidityPct, r.lightRaw, r.lightNorm, r.valid
```

### ActuatorController

```cpp
ActuatorController act(relayPin, ledPin, activeLow=true);
act.begin();
act.setRelay(RelayState::ON / OFF);
act.setRelayOverride(state, timeoutMs);   // expires after timeoutMs
act.setLedPattern(LedPattern::BLINK_SLOW / BLINK_FAST / BLINK_SOS / ON / OFF);
act.tick();                               // call in loop()
```

### MLInference

```cpp
MLInference ml;
ml.begin()                     // loads from tinyml_model.h
MLResult r = ml.infer(tempC, humPct, lightNorm);
// r.pNormal, r.pWarning, r.pCritical, r.riskScore, r.label, r.valid
```

---

## Module API — Raspberry Pi (Python)

All modules mirror the C++ API with Python naming conventions.

### StateManager

```python
from state_manager import StateManager, DeviceState
fsm = StateManager()
fsm.current()                   # → DeviceState enum
fsm.transition(DeviceState.X)   # → bool (False if illegal)
fsm.time_in_state_ms()          # → float ms
```

### IrisCamera

```python
from iris_camera import IrisCamera, IrisCapture
cam = IrisCamera()
cam.begin()                     # → bool; tries picamera2, falls back to OpenCV
c = cam.capture()               # → IrisCapture(features: np.ndarray[64], valid, timestamp_ms)
c = cam.capture_average(num_frames=5, delay_ms=200)
cam.close()
```

### BiometricManager

```python
from biometric_manager import BiometricManager, MatchResult
bio = BiometricManager()
bio.begin()                             # creates bio/ dir, loads registry
bio.enroll(user_id, name, iris)         # → bool; saves bio/{userId}_t{N}.npy
result = bio.match(iris, threshold)     # → MatchResult(matched, user_id, user_name, score)
bio.remove_user(user_id)               # → bool
bio.user_count()                        # → int
```

### AnomalyDetector

```python
from anomaly_detector import AnomalyDetector
anomaly = AnomalyDetector(match_threshold=0.30)
score = anomaly.record(result, success)   # → float [0.0, 1.0]
anomaly.brute_force_score()              # → float
anomaly.consecutive_failures()           # → int
```

### AlertManager

```python
from alert_manager import AlertManager
alerts = AlertManager(thing_name, aws_iot, firebase)
alerts.send_anomaly(user_id, score, alert_type, detail="")  # → bool
alerts.on_agent_ack(user_id, alert_type)
```

### FirebaseManager

```python
from firebase_manager import FirebaseManager
fb = FirebaseManager(api_key, db_url, email, password, device_id)
fb.begin()                                          # → bool; authenticates
fb.push_reading(sensor, ml, state)
fb.poll_commands()                                  # → RelayState or None
fb.send_heartbeat(state)
fb.flush_queue()
fb.push_sign_in(user_id, user_name, score, success, anomaly_score)
fb.push_enrollment(user_id, name)
fb.push_biometric_alert(user_id, alert_type, anomaly_score)
fb.poll_enroll_command()                            # → (user_id, name) or None
```

### AWSIoTManager

```python
from aws_iot_manager import AWSIoTManager
aws = AWSIoTManager(endpoint, thing_name, ca_path, cert_path, key_path)
aws.begin()                                        # → bool; connects paho-mqtt
aws.loop()                                         # no-op (paho uses loop_start)
aws.publish_biometric_event(user_id, score, success)
aws.publish_alert_json(json_str)                   # → bool
aws.get_agent_ack()                                # → (user_id, alert_type) or None
aws.get_relay_command()                            # → RelayState or None
```

### SensorManager

```python
from sensor_manager import SensorManager, SensorReading
sensors = SensorManager(dht_pin=4, ldr_spi_channel=0)
sensors.begin()
r = sensors.read_now()    # → SensorReading(temperature_c, humidity_pct, light_raw, light_norm, valid)
```

### ActuatorController

```python
from actuator_controller import ActuatorController, RelayState, LedPattern
act = ActuatorController(relay_pin=17, led_pin=27)
act.begin()
act.set_relay(RelayState.ON)
act.set_relay_override(RelayState.ON, timeout_ms=300_000)
act.set_led_pattern(LedPattern.BLINK_SLOW)
act.tick()     # call in main loop every ~10 ms
act.cleanup()  # call on shutdown
```

### MLInference

```python
from ml_inference import MLInference, MLResult
ml = MLInference(model_path="data/ambient_model.tflite")
ml.begin()                                 # → bool; loads .tflite file
r = ml.infer(temp_c, hum_pct, light_norm) # → MLResult(p_normal, p_warning, p_critical, risk_score, label, valid)
```

---

## AWS IoT MQTT Topics

| Topic | Direction | Description |
|---|---|---|
| `iot/{thing}/biometric/signin` | Device → Cloud | Authentication event telemetry |
| `iot/{thing}/biometric/alert` | Device → Cloud | Anomaly alert payload |
| `iot/{thing}/ai/alerts` | Cloud → Device | Bedrock Agent ACK |
| `iot/{thing}/telemetry` | Device → Cloud | Ambient sensor reading |
| `iot/{thing}/heartbeat` | Device → Cloud | Alive ping |
| `iot/{thing}/commands` | Cloud → Device | Remote relay command |
| `$aws/things/{thing}/shadow/update` | Device → Cloud | Device Shadow report |
| `$aws/things/{thing}/shadow/update/delta` | Cloud → Device | Shadow desired changes |

**Sign-in event payload** (`iot/{thing}/biometric/signin`):
```json
{
  "deviceId":  "rpi_node_01",
  "userId":    "john_doe",
  "matchScore": 0.143,
  "success":   true,
  "ts":        1713960305000
}
```

**Alert payload** (`iot/{thing}/biometric/alert`):
```json
{
  "deviceId":    "rpi_node_01",
  "userId":      "unknown",
  "alertType":   "brute_force",
  "anomalyScore": 0.82,
  "detail":      "Repeated failed iris attempts",
  "ts":          1713960400000
}
```

**Bedrock Agent ACK** (`iot/{thing}/ai/alerts`):
```json
{
  "userId":    "unknown",
  "alertType": "brute_force",
  "ack":       true
}
```

---

## Ambient ML Model

### Input tensor — shape `[1, 3]` float32

| Index | Feature | Normalisation |
|---|---|---|
| 0 | temperature_c | `(t − (−10)) / (60 − (−10))` → [0, 1] |
| 1 | humidity_pct | `h / 100.0` → [0, 1] |
| 2 | light_norm | already [0, 1] from ADC |

### Output tensor — shape `[1, 3]` float32 (softmax)

| Index | Class | Meaning |
|---|---|---|
| 0 | normal | System within safe parameters |
| 1 | warning | Elevated risk |
| 2 | critical | Immediate action required |

**Risk score** = `output[1] × 0.5 + output[2] × 1.0` ∈ [0, 1]

### Model file locations

| Platform | Format | Path |
|---|---|---|
| ESP32 | C byte array (`tinyml_model.h`) | `firmware/esp32_sensor_node/tinyml_model.h` |
| Raspberry Pi | `.tflite` binary | `firmware/rpi_sensor_node/data/ambient_model.tflite` |

Both are produced from the same Keras model. The converter step (`model_converter.py`) is only needed for ESP32.
