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

### Relay toggle doesn't affect device
1. Device polls `/devices/{id}/commands/relayOverride` every 5 s — wait up to 5 s
2. If device is in offline mode, it can't receive commands
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
