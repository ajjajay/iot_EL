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
  "location":    "Entrance A",
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
  "heartbeatIntervalMs": 30000,
  "tempWarningC":        35.0,
  "tempCriticalC":       45.0,
  "humidityWarningPct":  80.0,
  "mlRiskThreshold":     0.6,
  "awsEnabled":          true,
  "awsEndpoint":         "xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com",
  "awsThingName":        "esp32_node_01",
  "authButtonPin":       15,
  "irisMatchThreshold":  0.30,
  "irisEnrollFrames":    5,
  "authDisplayMs":       3000,
  "anomalyScoreThreshold": 0.60,
  "alertCooldownMs":     30000
}
```

Place at `firmware/esp32_sensor_node/data/config.json` then:
- Arduino IDE: Sketch → Show Sketch Folder → create `data/` → paste file
- Tools → ESP32 Sketch Data Upload (requires plugin)

### 8. AWS IoT Core setup (for biometric event routing)

1. **Create a Thing** in AWS Console → IoT Core → Manage → Things → Create
   - Name: `esp32_node_01` (match `awsThingName` in config.json)

2. **Create a Certificate** → download three files:
   - Amazon Root CA 1
   - Device certificate (.crt)
   - Private key (.key)
   Paste each into `firmware/esp32_sensor_node/aws_certificates.h`

3. **Create an IoT Policy** and attach it to the certificate:
```json
{
  "Version": "2012-10-17",
  "Statement": [{
    "Effect": "Allow",
    "Action": ["iot:Connect","iot:Publish","iot:Subscribe","iot:Receive"],
    "Resource": "arn:aws:iot:*:*:*"
  }]
}
```

4. **Create IoT Rules** to route biometric alerts to Lambda:
   - **Rule 1 — Biometric events** (sign-in telemetry → DynamoDB or S3):
     - SQL: `SELECT * FROM 'iot/+/biometric/signin'`
     - Action: Lambda (your anomaly ML function) or DynamoDB
   - **Rule 2 — Anomaly alerts** (trigger Bedrock Agent → user notification):
     - SQL: `SELECT * FROM 'iot/+/biometric/alert'`
     - Action: Lambda → `BiometricAlertAgent`

5. **Lambda `BiometricAlertAgent`** (Node.js example outline):
```javascript
exports.handler = async (event) => {
  const { deviceId, userId, alertType, anomalyScore } = event;
  // Option A: Invoke Bedrock Agent
  const bedrockClient = new BedrockAgentRuntimeClient({ region: "us-east-1" });
  await bedrockClient.send(new InvokeAgentCommand({
    agentId: process.env.BEDROCK_AGENT_ID,
    agentAliasId: process.env.BEDROCK_AGENT_ALIAS_ID,
    sessionId: `${deviceId}-${Date.now()}`,
    inputText: `Biometric anomaly on device ${deviceId}: ${alertType} for user ${userId}. Score: ${anomalyScore}. Notify the user.`
  }));
  // Option B: Direct SNS
  await snsClient.send(new PublishCommand({
    TopicArn: process.env.ALERT_SNS_TOPIC_ARN,
    Subject:  `Security Alert — ${alertType}`,
    Message:  `Device ${deviceId} flagged user ${userId} for ${alertType} (score ${(anomalyScore*100).toFixed(0)}%)`
  }));
  // Publish ACK back to ESP32 so it can log confirmation
  await iotClient.send(new PublishCommand({
    topic:   `iot/${deviceId}/ai/alerts`,
    payload: JSON.stringify({ userId, alertType, ack: true })
  }));
};
```

### 9. Enroll users via the dashboard

1. Open `frontend/index.html`
2. Go to **Enrolled Users → Enroll New User**
3. Select the device, enter a User ID and Full Name
4. Click **Send Enrollment Command**
5. The user should look at the camera within 30 seconds
6. The device captures 5 averaged iris frames and saves the template to SPIFFS

### 10. Verify Connection

Open Serial Monitor (115200 baud) after flashing:
```
=== Iris Biometric Access Control — Boot ===
[CFG] Loaded config for device 'esp32_node_01' @ 'Entrance A'
[CAM] Ready
[BIO] Ready — 0 user(s) enrolled
[WiFi] Connected — IP: 192.168.1.42  RSSI: -62 dBm
[FB]  Authenticated
[FB]  Device 'esp32_node_01' registered
[AWS] Connected to AWS IoT Core
[FSM] INIT → CONNECTING  (after 0ms, transition #1)
[FSM] READY → MONITORING (after 3210ms, transition #3)
[BOOT] Device 'esp32_node_01' ready — 0 user(s) enrolled
--- (press button to authenticate) ---
[BTN] Short press — starting authentication
[FSM] MONITORING → AUTHENTICATING
[CAM] Frame grab OK
[BIO] Match: john_doe score=0.1432 thresh=0.3000 → PASS
[ANOM] score=0.082  fail=0.00 prox=0.21 freq=0.00
[FB]  Sign-in logged: user=john_doe success=Y score=0.143
[AWS] Biometric event: user=john_doe success=Y score=0.143
[FSM] AUTHENTICATING → AUTHENTICATED
```

### Troubleshooting

| Error | Fix |
|---|---|
| `auth/invalid-api-key` | Double-check `firebaseApiKey` in config.json |
| `auth/user-not-found` | Create the device user in Firebase Console → Auth |
| `Permission denied` | Check database rules — switch to dev.rules for testing |
| `[CAM] Init failed` | Check camera pins; ensure board is AI Thinker ESP32-CAM |
| `[BIO] SPIFFS mount failed` | Use "Huge APP" partition scheme; run SPIFFS data upload |
| `[AWS] MQTT connect failed` | Verify endpoint, certificate, and policy in aws_certificates.h |
| Match score always high (>0.4) | Re-enroll in consistent lighting; ensure IR light source is present |
