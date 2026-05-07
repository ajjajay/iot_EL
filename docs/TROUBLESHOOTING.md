# Troubleshooting Guide

Issues are grouped by subsystem. Check Serial Monitor (ESP32) or terminal output (RPi) first — every module logs a `[TAG]` prefix.

---

## Biometric / Camera — both platforms

### Match score always > 0.4 — all attempts rejected

1. **Poor lighting** — iris recognition needs near-infrared (NIR) illumination; add an IR LED pointed at the eye
2. **Camera too far** — position 15–30 cm from the eye for useful feature extraction
3. **Enrolled in different lighting** — re-enroll under the same conditions you will use for auth
4. **`irisMatchThreshold` too strict** — raise from `0.30` to `0.35` in `config.json` for lower-quality setups
5. **Enrollment with invalid frames** — check log for `[CAM] Frame grab failed` during enrollment; clear templates and re-enroll

### Enrollment command from dashboard not received

1. Device must be in **MONITORING** state — check FSM log
2. Firebase path must be `/devices/{deviceId}/commands/enroll/pending: true` — verify in Firebase Console
3. Wrong device selected in dashboard — confirm `deviceId` matches
4. Command poll runs every 5 s — wait and watch for `[FB] Enrollment command pending` in logs

### Authentication button not responding

1. `authButtonPin` mismatch — check config.json vs actual wiring
2. Missing pull-up — button must be active LOW with pull-up to 3.3V (or enable internal pull-up)
3. `buttonDebounceMs` too high — try lowering to `30` in config.json
4. FSM not in MONITORING — button input is only checked in that state

---

## ESP32-CAM Specific

### Device stuck in CONNECTING

1. Wrong WiFi SSID / password in `config.json` — ESP32 only supports **2.4 GHz**
2. Firebase URL wrong or database not yet created — check `firebaseUrl` ends in `.firebaseio.com`
3. Firebase Auth user not created — Console → Authentication → Users → Add user
4. Router MAC filtering — add ESP32 MAC to allowlist

### `[CAM] Init failed`

1. Wrong board — must be **AI Thinker ESP32-CAM** (OV2640)
2. Wrong partition scheme — must use **"Huge APP"** (3 MB app / 1 MB SPIFFS)
3. GPIO conflict — no other code may reconfigure GPIOs 0, 2, 4, 5, 12–15, 17–19, 21–23, 25–27
4. Power brownout — camera draws up to 200 mA burst; add 470–1000 µF capacitor on 3.3V rail

### `[BIO] SPIFFS mount failed`

1. Partition scheme mismatch — select **"Huge APP"** in Arduino IDE → Tools → Partition Scheme
2. Re-upload SPIFFS — Tools → ESP32 Sketch Data Upload after correcting the partition scheme
3. SPIFFS corrupted — the first boot auto-formats; if it fails, re-upload SPIFFS data

### ESP32 crashes / reboots in a loop

Check Serial Monitor for panic details:
- `LoadProhibited` → null pointer, usually ML model not loaded; verify `ml.begin()` returned `true`
- Stack overflow → increase `TENSOR_ARENA_SIZE` in `MLInference.h`
- Brownout → add 100–470 µF capacitor near ESP32 VCC pin
- Camera init crash → verify AI Thinker board and "Huge APP" partition

### `[ML] Invoke() failed` (ESP32)

1. Still using stub `tinyml_model.h` → run `models/model_training.py` then `model_converter.py`
2. Tensor arena too small → increase `TENSOR_ARENA_SIZE` in `MLInference.h` (default 8 KB)
3. Library version mismatch → update `TensorFlowLite_ESP32` in Library Manager

### Firebase `Permission denied` (ESP32)

1. Using prod.rules without matching `auth.uid` → switch to `dev.rules` during development
2. Auth token expired → verify Firebase email/password in `config.json` are correct
3. Device ID in RTDB rules doesn't match the authenticated UID

---

## Raspberry Pi Specific

### `[CAM] No camera backend found`

1. **picamera2**: ensure the camera ribbon is seated, the camera interface is enabled (`sudo raspi-config` → Interface Options → Camera), and `picamera2` is installed (`pip install picamera2`)
2. **OpenCV fallback**: USB webcam not detected — run `v4l2-ctl --list-devices` to confirm the device path; try `cv2.VideoCapture(0)` in a Python shell
3. Set `SENSOR_MOCK=1` to test without any camera hardware

### `[SENS] DHT22 init failed`

1. Install Blinka and the DHT library: `pip install adafruit-blinka adafruit-circuitpython-dht`
2. Verify `dhtPin` in config.json uses BCM numbering, not physical pin number
3. Add a 10kΩ pull-up resistor between the DHT22 data line and 3.3V
4. Some RPi OS versions need `use_pulseio=False` — the `sensor_manager.py` already passes this flag

### `[SENS] MCP3008 init failed (LDR will read 0)`

1. Enable SPI: `sudo raspi-config` → Interface Options → SPI → Enable, then reboot
2. Install library: `pip install adafruit-circuitpython-mcp3xxx`
3. Check wiring: MCP3008 VDD and VREF to 3.3V; AGND and DGND to GND; connect MOSI/MISO/SCLK/CS to SPI0 pins
4. LDR readings of 0 are non-fatal — the system continues with `light_norm = 0.0`

### `[AWS] begin() failed` / TLS error (RPi)

1. Certificate paths in `config.json` (`awsCaPath`, `awsCertPath`, `awsKeyPath`) must be relative to `rpi_sensor_node/` or absolute
2. `ssl.PROTOCOL_TLS_CLIENT` requires Python 3.6+; on older Python, change to `ssl.PROTOCOL_TLS`
3. Verify `paho-mqtt` version ≥ 1.6.1 — older versions have different TLS API
4. Check `awsEndpoint` is the device data endpoint (ends in `-ats.iot.<region>.amazonaws.com`)

### `[FB] Auth failed` (RPi)

1. Firebase Email/Password auth not enabled — Console → Authentication → Sign-in method
2. Wrong `firebaseEmail` / `firebasePass` in config.json
3. `requests` not installed: `pip install requests`
4. Token auto-refreshes every hour; if refresh fails, restart the service

### RPi service not auto-starting

```bash
# Check service status
sudo systemctl status iris-node

# View logs
sudo journalctl -u iris-node -f

# Restart
sudo systemctl restart iris-node
```

Ensure the `WorkingDirectory` in the `.service` file points to `firmware/rpi_sensor_node/`.

---

## AWS IoT / MQTT — both platforms

### MQTT connect failed

1. Wrong `awsEndpoint` — find it in AWS Console → IoT Core → Settings → Device data endpoint
2. Certificate mismatch — verify certificates belong to the correct Thing
3. Policy too restrictive — policy must allow `iot:Connect`, `iot:Publish`, `iot:Subscribe`, `iot:Receive`
4. `awsThingName` in config.json must exactly match the Thing name in AWS Console

### Biometric alerts not triggering Lambda

1. IoT Rule SQL: `SELECT * FROM 'iot/+/biometric/alert'` (single quotes, `+` wildcard)
2. Rule must be **Active** — AWS Console → IoT Core → Act → Rules
3. Lambda ARN must be correct in the rule action
4. Lambda execution role needs `iot:Publish` permission to publish the ACK back

### Agent ACK never received

1. Lambda not publishing back — confirm Lambda publishes to `iot/{deviceId}/ai/alerts` with `{ "ack": true, "userId": ..., "alertType": ... }`
2. Device not subscribed — check logs for `Subscribed` confirmation at connect time
3. QoS 0 can be dropped — add retry logic in Lambda for critical alerts

---

## Dashboard

### "Running in demo mode"

`FIREBASE_CONFIG` in `app.js` still has placeholder values. Replace all `REPLACE_WITH_YOUR_*` fields with your actual Firebase project config.

### Charts empty / not updating

1. Check browser console for Firebase auth errors
2. Verify device is pushing to `/readings/{deviceId}` — check Firebase Console → Realtime Database → Data
3. Confirm the dashboard user has read permission under the database rules

### Sign-in log empty

1. Device must be performing authentications — watch FSM transitions in logs
2. Check `/signins/{deviceId}` in Firebase Console
3. Confirm prod.rules allow the device UID to write to `/signins/{deviceId}`

### Relay toggle has no effect

1. Device polls `/devices/{id}/commands/relayOverride` every 5 s — wait up to 5 s
2. Override commands are ignored while device is in AUTHENTICATING or ENROLLING
3. Check logs for `[ACT] Manual relay override` or `[ACT] Relay override expired`

---

## ML Model Training

### `ModuleNotFoundError: No module named 'tensorflow'`

```bash
pip install -r models/requirements.txt
# Apple Silicon:
pip install tensorflow-macos tensorflow-metal
```

### `model_converter.py: model.tflite not found`

Run `model_training.py` before `model_converter.py`. The training script writes `model.tflite` in the `models/` directory.

### Low training accuracy (< 80%)

1. Check class balance in the generated CSV
2. Increase epochs: `python model_training.py --epochs 200`
3. Verify label thresholds in `mock_data_generator.py` match the firmware thresholds

### RPi: `tflite-runtime` install fails

```bash
# Standard install
pip install tflite-runtime

# If that fails, install full TensorFlow (larger but always works)
pip install tensorflow

# The ml_inference.py module falls back to tensorflow.lite automatically
```

---

## Hardware Quick Reference

| Issue | First check |
|---|---|
| DHT22 reads 0°C / 0% | 10kΩ pull-up resistor on DATA pin? |
| LDR reads max (4095 / 1023) | Voltage divider wired? GND connected to MCP3008 AGND? |
| Relay clicks but nothing happens | Load wired to NO (normally-open) terminal, not NC? |
| LED never blinks | `ledPin` BCM number mismatch? `RPi.GPIO` vs physical numbering? |
| Cannot flash ESP32 | Hold BOOT button during upload; release after upload starts |
| ESP32-CAM camera all black | ESP32-CAM needs stable 5V @ ≥ 500 mA; power from dedicated supply |
| Iris match always fails | Check IR illumination; add IR LED pointed at eye; re-enroll |
| RPi camera all black | `vcgencmd get_camera` should show `supported=1 detected=1` |
