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

### Latest Reading (updated every `sensorIntervalMs`)

| Path | Type | Range | Description |
|---|---|---|---|
| `/devices/{id}/latest/temperatureC` | float | -40 – 80 | Temperature in °C |
| `/devices/{id}/latest/humidityPct` | float | 0 – 100 | Relative humidity % |
| `/devices/{id}/latest/lightRaw` | int | 0 – 4095 | Raw 12-bit ADC value |
| `/devices/{id}/latest/lightNorm` | float | 0.0 – 1.0 | Normalised light level |
| `/devices/{id}/latest/riskScore` | float | 0.0 – 1.0 | ML composite risk score |
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

### Remote Config (written by admin, read by device at boot)

| Path | Type | Description |
|---|---|---|
| `/devices/{id}/config/tempWarningC` | float | Override warning threshold |
| `/devices/{id}/config/tempCriticalC` | float | Override critical threshold |
| `/devices/{id}/config/humidityWarningPct` | float | Override humidity threshold |
| `/devices/{id}/config/mlRiskThreshold` | float | Override ML risk threshold |
| `/devices/{id}/config/sensorIntervalMs` | int | Override polling interval |

### Time-series Readings

| Path | Type | Description |
|---|---|---|
| `/readings/{id}/{pushId}` | object | Full reading snapshot (same fields as `/latest`) |

> Readings are appended via Firebase `push()` which generates a time-ordered push ID key.
> Use `.limitToLast(N)` to fetch the N most recent readings.

### Alerts

| Path | Type | Description |
|---|---|---|
| `/alerts/{id}/{pushId}/deviceId` | string | Source device |
| `/alerts/{id}/{pushId}/riskScore` | float | Risk score at alert time |
| `/alerts/{id}/{pushId}/mlLabel` | int | ML classification at alert |
| `/alerts/{id}/{pushId}/temperatureC` | float | Temperature at alert time |
| `/alerts/{id}/{pushId}/ts` | number | JS timestamp (ms) |
| `/alerts/{id}/{pushId}/acknowledged` | boolean | Set true by dashboard |

---

## C++ Module API

### StateManager

```cpp
StateManager fsm;
fsm.current()            // → DeviceState enum
fsm.transition(next)     // → bool (false if illegal transition)
fsm.timeInState()        // → unsigned long ms in current state
fsm.transitionCount()    // → uint32_t total transitions

stateToString(state)     // → const char* ("MONITORING", etc.)
```

### SensorManager

```cpp
SensorManager sensors(dhtPin, dhtType, ldrPin, emaAlpha=0.3f);
sensors.begin();
SensorReading r = sensors.readNow();
// r.temperatureC, r.humidityPct, r.lightRaw, r.lightNorm, r.valid, r.timestampMs
sensors.failCount()      // consecutive failures since last success
```

### ActuatorController

```cpp
ActuatorController act(relayPin, ledPin, activeLow=true);
act.begin();
act.setRelay(RelayState::ON / ::OFF);
act.setRelayOverride(state, timeoutMs);  // dashboard override
act.clearRelayOverride();
act.hasRelayOverride()     // bool
act.setLedPattern(LedPattern::BLINK_SLOW / ::BLINK_FAST / ::BLINK_SOS / ...);
act.tick();                // call in loop() — services blink patterns
```

### MLInference

```cpp
MLInference ml;
ml.begin()              // → bool; loads model from tinyml_model.h
MLResult r = ml.infer(tempC, humPct, lightNorm);
// r.pNormal, r.pWarning, r.pCritical, r.riskScore, r.label, r.valid
```

### FirebaseManager

```cpp
FirebaseManager fb(apiKey, dbUrl, email, password, deviceId);
fb.begin()                           // → bool; authenticates + registers
fb.pushReading(reading, ml, state)  // push or enqueue if offline
fb.pollCommands(outRelay)           // → bool if command changed
fb.sendHeartbeat(state)
fb.flushQueue()                     // drain offline ring buffer
fb.setOnline(bool)
fb.isConnected()                    // → bool
```

### ConfigManager

```cpp
ConfigManager config;
config.load()                        // loads /config.json from SPIFFS
config.save()                        // writes current config to SPIFFS
const DeviceConfig& c = config.cfg();
config.setThresholds(tempWarn, tempCrit, humWarn, lightLow);
```

---

## ML Model API

### Input tensor

Shape: `[1, 3]` float32

| Index | Feature | Normalisation |
|---|---|---|
| 0 | temperature_c | `(t - (-10)) / (60 - (-10))` → [0, 1] |
| 1 | humidity_pct | `h / 100.0` → [0, 1] |
| 2 | light_norm | already [0, 1] |

### Output tensor

Shape: `[1, 3]` float32 (softmax)

| Index | Class | Meaning |
|---|---|---|
| 0 | normal | System within safe parameters |
| 1 | warning | Elevated risk; monitor closely |
| 2 | critical | Immediate action required |

**Risk score** = `output[1] × 0.5 + output[2] × 1.0` ∈ [0, 1]
