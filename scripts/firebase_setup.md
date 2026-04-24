# Firebase Setup Guide

## Step-by-step from zero to live

### 1. Create Firebase Project

```
https://console.firebase.google.com → Add project
Name: iot-monitor (or any name)
Google Analytics: optional for IoT
```

### 2. Enable Realtime Database

```
Console → Build → Realtime Database → Create Database
Region: choose nearest (e.g. us-central1)
Security rules: Start in test mode (we'll apply real rules next)
```

Copy the **Database URL** — looks like:
`https://your-project-id-default-rtdb.firebaseio.com`

### 3. Enable Email/Password Auth

```
Console → Build → Authentication → Sign-in method
Enable: Email/Password
```

Create users (one per device + one for dashboard):
```
Console → Authentication → Users → Add user

User 1: esp32-node-01@yourdomain.com  | <strong random password>
User 2: esp32-node-02@yourdomain.com  | <strong random password>
User 3: dashboard@yourdomain.com      | <dashboard password>
```

Save device credentials — they go into each device's `config.json`.

### 4. Get Web App Config

```
Console → Project settings (gear) → General → Your apps
Click: Add app → Web (</>)
Register app name: iot-dashboard
Copy the firebaseConfig object
```

Paste into `frontend/app.js` → `FIREBASE_CONFIG`.

### 5. Apply Database Security Rules

**Development** — paste `firebase/dev.rules` content into:
```
Console → Realtime Database → Rules tab → Paste → Publish
```

**Production** — use Firebase CLI:
```bash
npm install -g firebase-tools
firebase login
firebase init database    # select project, use firebase/prod.rules
firebase deploy --only database
```

### 6. Seed Device Config on Firebase

Optional — push initial config from the dashboard or via Firebase Console:
```
Console → Realtime Database → Data → + → /devices/esp32_node_01/config
Paste values from firebase/schema.json
```

### 7. Device SPIFFS config.json

Create one per device. Upload via Arduino IDE ESP32 SPIFFS Uploader:
```json
{
  "deviceId":    "esp32_node_01",
  "location":    "Lab Room A",
  "wifiSsid":    "YourWiFi",
  "wifiPassword":"YourPassword",
  "firebaseApiKey":    "AIzaSy...",
  "firebaseUrl":       "https://your-project-default-rtdb.firebaseio.com",
  "firebaseEmail":     "esp32-node-01@yourdomain.com",
  "firebasePass":      "DevicePassword",
  "dhtPin":      4,
  "ldrPin":      34,
  "relayPin":    26,
  "ledPin":      2,
  "dhtType":     22,
  "sensorIntervalMs":    5000,
  "firebaseSyncMs":      10000,
  "heartbeatIntervalMs": 30000,
  "tempWarningC":        35.0,
  "tempCriticalC":       45.0,
  "humidityWarningPct":  80.0,
  "lightLowThreshold":   200,
  "mlRiskThreshold":     0.6
}
```

Place at `firmware/esp32_sensor_node/data/config.json` then:
- Arduino IDE: Sketch → Show Sketch Folder → create `data/` → paste file
- Tools → ESP32 Sketch Data Upload (requires plugin)

### 8. Verify Connection

Open Serial Monitor (115200 baud) after flashing:
```
[CFG] Loaded config for device 'esp32_node_01' @ 'Lab Room A'
[ML]  Model loaded OK — arena used: 4200 / 8192 bytes
[WiFi] Connected — IP: 192.168.1.42  RSSI: -62 dBm
[FB]  Authenticated
[FB]  Device 'esp32_node_01' registered
[FSM] INIT → CONNECTING  (after 0ms, transition #1)
[FSM] CONNECTING → READY (after 3210ms, transition #2)
[FSM] READY → MONITORING (after 10ms, transition #3)
[SENS] T=24.3°C  H=58.1%  L=2048
[ML]  n=0.912 w=0.072 c=0.016 → risk=0.052 label=0
[FB]  Pushed: T=24.3 H=58.1 L=2048 risk=0.052
```

### Troubleshooting Auth Errors

| Error | Fix |
|---|---|
| `auth/invalid-api-key` | Double-check `firebaseApiKey` in config.json |
| `auth/user-not-found` | Create the device user in Firebase Console → Auth |
| `auth/wrong-password` | Verify password in config.json matches Auth user |
| `Permission denied` | Check database rules — switch to dev.rules for testing |
| `Network error` | Check WiFi credentials; ensure Firebase URL has no trailing `/` |
