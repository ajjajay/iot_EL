# Troubleshooting Guide

## Firmware / Serial

### Device stuck in CONNECTING state
**Symptom**: Serial shows repeated `[WiFi] Connecting...` dots indefinitely.

**Causes & fixes**:
1. Wrong SSID/password in `config.json` → verify spelling (case-sensitive)
2. 5 GHz WiFi — ESP32 only supports 2.4 GHz
3. Router MAC filtering enabled → add ESP32 MAC to allowlist
4. Firebase URL missing or wrong → check `firebaseUrl` in config.json
5. Firebase Auth user not created → Console → Auth → Users → Add user

---

### Sensor reads return NaN / `[SENS] All retries failed`
**Causes & fixes**:
1. DHT pin mismatch → verify `dhtPin` in config.json matches physical wiring
2. Wrong DHT type (11 vs 22) → change `dhtType` in config.json
3. Missing pull-up resistor on DHT data line → add 10kΩ between DATA and 3.3V
4. Insufficient power — DHT22 needs at least 3 mA → check power supply
5. Too-rapid polling — DHT22 minimum interval is 2 s → `sensorIntervalMs` ≥ 2000

---

### ML inference fails (`[ML] Invoke() failed`)
**Causes & fixes**:
1. Using stub `tinyml_model.h` → run training + converter scripts
2. Tensor arena too small → increase `TENSOR_ARENA_SIZE` in MLInference.h
3. Wrong TFLite schema version → update TensorFlowLite_ESP32 library
4. Insufficient heap → reduce other allocations or use `psramInit()` on boards with PSRAM

---

### Firebase push fails (`Permission denied`)
**Causes & fixes**:
1. Database rules too restrictive → switch to `dev.rules` during development
2. Auth token expired → `Firebase.reconnectWiFi(true)` is already enabled; check if auth user email/pass correct
3. Device ID in rules doesn't match `auth.uid` → in Firebase Auth, UID ≠ email; use custom tokens or open rules for dev

---

### Relay activates backwards (ON = GPIO LOW, OFF = GPIO HIGH)
**Fix**: In config.json, add `"relayActiveLow": false` — or invert in `ActuatorController.cpp` constructor default.

---

### ESP32 crashes / reboots in a loop
**Check** Serial Monitor for panic reason:
- `Guru Meditation Error: Core panic'ed` + `LoadProhibited` → null pointer in ML; ensure `ml.begin()` returned true
- Stack overflow → increase `TENSOR_ARENA_SIZE` or check for deep recursion
- Brownout → power supply too weak; add 100–470 µF capacitor near ESP32 VCC pin
- Camera init crash → ensure you are using ESP32-CAM (AI Thinker) with correct pinout; check `IrisCamera.h` pin assignments

---

## Biometric / Camera

### `[CAM] Init failed`
**Causes & fixes**:
1. Wrong board — must be **AI Thinker ESP32-CAM** (OV2640 sensor)
2. Wrong partition scheme — must use **"Huge APP"** (3 MB app / 1 MB SPIFFS) to fit camera driver + TFLite
3. Camera pins overridden elsewhere — ensure no other code reconfigures GPIO 0, 2, 4, 5, 12–15, 17–19, 21–23, 25–27
4. Power brownout when camera starts — add 470–1000 µF capacitor on 3.3V rail; camera draws up to 200 mA burst

---

### `[BIO] SPIFFS mount failed`
**Causes & fixes**:
1. Partition scheme mismatch — select **"Huge APP"** in Arduino IDE → Tools → Partition Scheme
2. SPIFFS never formatted — upload sketch once; first boot auto-formats if needed
3. Corrupted SPIFFS — use **Tools → ESP32 Sketch Data Upload** after selecting the right partition scheme to re-upload `data/config.json`

---

### Match score always high (> 0.4), all attempts rejected
**Causes & fixes**:
1. Poor lighting — iris recognition requires near-infrared (NIR) illumination; add IR LED pointed at face
2. Camera too far — position camera 15–30 cm from the eye for best feature extraction
3. Enrolled in different lighting than when matching — re-enroll under the same conditions
4. Enrollment with too few valid frames → check Serial for `[CAM] Frame invalid` during enrollment; clear and re-enroll
5. `irisMatchThreshold` too strict — raise from 0.30 to 0.35 in `config.json` for lower-quality setups

---

### `[BIO] Enroll failed — user limit reached`
**Fix**: Maximum 16 users (`BIO_MAX_USERS`). Remove inactive users via dashboard → Enrolled Users → Remove.

---

### Enrollment command from dashboard not received by device
**Causes & fixes**:
1. Device not polling Firebase — check Serial for `[FB] pollEnrollCommand` every 5 s
2. Firebase path mismatch — command must be written to `/devices/{deviceId}/commands/enroll/pending: true`
3. Wrong device selected in dashboard enroll form — verify the device ID matches the target device
4. Device is in AUTHENTICATING or ENROLLING state — wait for it to return to MONITORING

---

### Authentication button not responding
**Causes & fixes**:
1. `authButtonPin` mismatch in config.json → verify with Serial print of `c.authButtonPin`
2. Button not pulled up → add 10kΩ from button pin to 3.3V (or enable internal pull-up in firmware)
3. Button debounce too aggressive → lower `buttonDebounceMs` in config.json (default 50 ms)
4. FSM not in MONITORING state → device must be MONITORING before button is checked

---

## AWS IoT / MQTT

### `[AWS] MQTT connect failed`
**Causes & fixes**:
1. Wrong endpoint in `awsEndpoint` / `config.json` — find it in AWS Console → IoT Core → Settings → Device data endpoint
2. Certificate mismatch — verify content of `aws_certificates.h` matches the downloaded files for that Thing
3. Policy too restrictive — ensure the policy allows `iot:Connect`, `iot:Publish`, `iot:Subscribe`, `iot:Receive`
4. Thing name mismatch — `awsThingName` in config.json must exactly match the Thing name in IoT Core

---

### Biometric alerts not triggering Lambda
**Causes & fixes**:
1. IoT Rule SQL wrong — confirm rule SQL is: `SELECT * FROM 'iot/+/biometric/alert'` (note single quotes, wildcard)
2. Rule disabled — AWS Console → IoT Core → Act → Rules → check rule is Active
3. Lambda not configured in rule action → rule action must point to the correct Lambda ARN
4. Lambda execution role missing `iot:Publish` permission → add IoT publish to the Lambda execution role

---

### Agent ACK never received (`[ALERT] No ACK from agent`)
**Causes & fixes**:
1. Lambda not publishing back — verify Lambda publishes to `iot/{deviceId}/ai/alerts`
2. Device not subscribed — `_reconnect()` subscribes at connect time; check `[AWS] Subscribed to ai/alerts` in Serial
3. MQTT QoS 0 drop — add retry logic in Lambda if reliability is critical
4. `agentAckPending` check missed — ensure `alertMgr->onAgentAck()` is called in main loop after `awsIoT->getAgentAck()`

---

## Dashboard

### Dashboard shows "running in demo mode"
**Cause**: `FIREBASE_CONFIG` in `app.js` has placeholder values.
**Fix**: Replace all `REPLACE_WITH_YOUR_*` values with your actual Firebase project config.

---

### Charts are empty / not updating
1. Check browser console for Firebase auth errors
2. Verify device is pushing to `/readings/{deviceId}` (check Firebase Console → Data)
3. Ensure dashboard user has read permission (check database rules)

---

### Sign-in log shows no entries
1. Verify device is actually authenticating (check Serial Monitor)
2. Check `/signins/{deviceId}` in Firebase Console → Realtime Database → Data
3. Confirm prod.rules allow the device UID to write to `/signins/{deviceId}`

---

### Enrolled Users section empty after enrollment
1. Check `/users/{userId}` in Firebase Console — entry must exist
2. `FirebaseManager.pushEnrollment()` must succeed → look for `[FB] Enrollment logged` in Serial
3. Dashboard `/users` listener uses `.on('value')` — force a browser refresh if listener was added before data appeared

---

### Relay toggle doesn't affect device
1. Device polls `/devices/{id}/commands/relayOverride` every 5 s — wait up to 5 s
2. If device is in AUTHENTICATING or ENROLLING state it ignores relay override commands
3. Check Serial Monitor for `[ACT] Manual relay override` log

---

## Firebase

### "Firebase Realtime Database not found" error
The project may be using Firestore instead of Realtime Database.
Go to Console → Build → Realtime Database → Create Database (create it explicitly).

### High read costs
By default the dashboard's `onValue("/devices")` re-downloads the full subtree on any change.
For production with many devices, switch to per-device listeners and use `limitToLast(1)` on readings.

### Rules not taking effect
Firebase rule changes can take up to 1 minute to propagate. Wait and retry.

---

## Python (model training)

### `ModuleNotFoundError: No module named 'tensorflow'`
```bash
pip install -r models/requirements.txt
```
On Apple Silicon: `pip install tensorflow-macos tensorflow-metal`

### Training accuracy < 80%
1. Check class imbalance in dataset
2. Increase epochs: `--epochs 200`
3. Verify label logic matches firmware threshold rules
4. Check that EMA smoothing didn't distort the real-sensor data

### `model_converter.py` — `model.tflite not found`
You must run `model_training.py` before `model_converter.py`.

---

## Hardware Quick Reference

| Issue | Check first |
|---|---|
| DHT22 reading 0°C / 0% | Pull-up resistor on DATA pin? |
| LDR always reads 4095 | Voltage divider wired correctly? GND connected? |
| Relay clicks but nothing happens | Is load wired to NO (normally-open) or NC? |
| LED never blinks | `ledPin` GPIO mismatch? Onboard LED is GPIO 2 on most dev boards |
| Can't upload firmware | Hold BOOT button on ESP32 during upload |
| Camera shows all black frames | Power rail insufficient — ESP32-CAM needs stable 5V @ 500 mA |
| Iris match always fails | Check IR illumination; test with `captureAverage` debug output |
| SPIFFS upload silently skipped | Must install ESP32 SPIFFS Uploader plugin; select correct COM port |
