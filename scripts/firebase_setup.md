# Setup Guide — Firebase + AWS + Edge Nodes

Complete from-scratch setup for one or more ESP32-CAM or Raspberry Pi nodes.

---

## Part 1 — Firebase

### 1. Create Firebase Project

```
https://console.firebase.google.com → Add project
Name: iris-access-control
Google Analytics: disable (not needed)
```

### 2. Enable Realtime Database

```
Console → Build → Realtime Database → Create Database
Region: choose nearest (e.g. us-central1)
Security rules: Start in test mode
```

Copy the **Database URL** — looks like:
`https://iris-access-control-default-rtdb.firebaseio.com`

### 3. Enable Email/Password Auth

```
Console → Build → Authentication → Get started → Sign-in method
Enable: Email/Password
```

Create one account per device node **and** one for the dashboard:
```
Console → Authentication → Users → Add user

ESP32-CAM node 1:  esp32-node-01@yourdomain.com  | <strong-password>
ESP32-CAM node 2:  esp32-node-02@yourdomain.com  | <strong-password>
Raspberry Pi node: rpi-node-01@yourdomain.com    | <strong-password>
Dashboard:         dashboard@yourdomain.com      | <dashboard-password>
```

Save these credentials — they go into each device's `config.json`.

### 4. Get Web App Config

```
Console → Project settings (gear) → General → Your apps
Click: Add app → Web (</>)
Register app name: iris-dashboard
Copy the firebaseConfig object
```

Paste into:
- `frontend/app.js` → `FIREBASE_CONFIG`
- Each device's `config.json` (see Step 5)

### 5. Apply Database Security Rules

**Development** (paste into Firebase Console → Realtime Database → Rules):
```
firebase/dev.rules   ← open read/write for authenticated users
```

**Production** (before going live):
```bash
npm install -g firebase-tools
firebase login
firebase init database   # select your project
# copy firebase/prod.rules → database.rules.json
firebase deploy --only database
```

---

## Part 2 — AWS IoT Core

### 6. Create a Thing (one per device)

AWS Console → IoT Core → Manage → Things → Create things → Create single thing
- Name: `esp32_node_01` (or `rpi_node_01`) — must match `awsThingName` in config.json

### 7. Download Certificates

During Thing creation, select **Auto-generate a new certificate**. Download:
- **Device certificate** (`.crt`)
- **Private key** (`.key`)
- **Amazon Root CA 1** (`root-ca.crt`)

**For ESP32**: paste all three into `firmware/esp32_sensor_node/aws_certificates.h`

**For RPi**: copy all three into `firmware/rpi_sensor_node/data/`:
```bash
cp AmazonRootCA1.pem firmware/rpi_sensor_node/data/root-ca.crt
cp XXXXXXXXXXXX-certificate.pem.crt firmware/rpi_sensor_node/data/device.crt
cp XXXXXXXXXXXX-private.pem.key firmware/rpi_sensor_node/data/device.key
```

### 8. Create and Attach an IoT Policy

AWS Console → IoT Core → Security → Policies → Create policy:
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

Attach the policy to the device certificate.

### 9. Create IoT Rules

**Rule 1 — Biometric sign-in events** (telemetry):
- SQL: `SELECT * FROM 'iot/+/biometric/signin'`
- Action: Lambda or DynamoDB (optional — sign-ins are also logged to Firebase)

**Rule 2 — Anomaly alerts** (triggers AI notification):
- SQL: `SELECT * FROM 'iot/+/biometric/alert'`
- Action: Lambda → `BiometricAlertAgent`

### 10. Deploy Lambda: BiometricAlertAgent

Create a Lambda function (Node.js 18+) with the following logic:

```javascript
const { BedrockAgentRuntimeClient, InvokeAgentCommand } = require("@aws-sdk/client-bedrock-agent-runtime");
const { SNSClient, PublishCommand } = require("@aws-sdk/client-sns");
const { IoTDataPlaneClient, PublishCommand: IotPublish } = require("@aws-sdk/client-iot-data-plane");

exports.handler = async (event) => {
  const { deviceId, userId, alertType, anomalyScore } = event;

  // Option A: Invoke Bedrock Agent for AI-generated notification
  const bedrock = new BedrockAgentRuntimeClient({ region: "us-east-1" });
  await bedrock.send(new InvokeAgentCommand({
    agentId:      process.env.BEDROCK_AGENT_ID,
    agentAliasId: process.env.BEDROCK_AGENT_ALIAS_ID,
    sessionId:    `${deviceId}-${Date.now()}`,
    inputText:    `Security alert on device ${deviceId}: ${alertType} for user ${userId}. Anomaly score: ${anomalyScore}. Please notify the user.`
  }));

  // Option B: Direct SNS notification (simpler)
  const sns = new SNSClient({ region: "us-east-1" });
  await sns.send(new PublishCommand({
    TopicArn: process.env.ALERT_SNS_TOPIC_ARN,
    Subject:  `[Iris Alert] ${alertType} on ${deviceId}`,
    Message:  `User: ${userId}\nType: ${alertType}\nScore: ${(anomalyScore * 100).toFixed(0)}%`
  }));

  // Publish ACK back to device to close the feedback loop
  const iot = new IoTDataPlaneClient({ region: "us-east-1" });
  await iot.send(new IotPublish({
    topic:   `iot/${deviceId}/ai/alerts`,
    payload: JSON.stringify({ userId, alertType, ack: true })
  }));
};
```

Lambda execution role needs: `iot:Publish`, `sns:Publish`, `bedrock:InvokeAgent`.

---

## Part 3 — Device Config

### 11. Configure ESP32 Node

Edit `firmware/esp32_sensor_node/data/config.json`:

```json
{
  "deviceId":    "esp32_node_01",
  "location":    "Entrance A",
  "wifiSsid":    "YourWiFi",
  "wifiPassword":"YourPassword",
  "firebaseApiKey":    "YOUR_FIREBASE_API_KEY",
  "firebaseUrl":       "https://YOUR_PROJECT-default-rtdb.firebaseio.com",
  "firebaseEmail":     "esp32-node-01@yourdomain.com",
  "firebasePass":      "DevicePassword",
  "dhtPin":      4,
  "ldrPin":      34,
  "relayPin":    26,
  "ledPin":      2,
  "dhtType":     22,
  "authButtonPin":       15,
  "irisMatchThreshold":  0.30,
  "irisEnrollFrames":    5,
  "authDisplayMs":       3000,
  "anomalyScoreThreshold": 0.60,
  "alertCooldownMs":     30000,
  "sensorIntervalMs":    10000,
  "heartbeatIntervalMs": 30000,
  "mlRiskThreshold":     0.65,
  "awsEnabled":          true,
  "awsEndpoint":         "xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com",
  "awsThingName":        "esp32_node_01"
}
```

Then in Arduino IDE:
```
Tools → Board → AI Thinker ESP32-CAM
Tools → Partition Scheme → Huge APP (3 MB app / 1 MB SPIFFS)
Tools → ESP32 Sketch Data Upload      ← uploads config.json to SPIFFS
Sketch → Upload                       ← flashes firmware
```

### 12. Configure Raspberry Pi Node

Edit `firmware/rpi_sensor_node/data/config.json`:

```json
{
  "deviceId":    "rpi_node_01",
  "location":    "Entrance B",
  "firebaseApiKey":    "YOUR_FIREBASE_API_KEY",
  "firebaseUrl":       "https://YOUR_PROJECT-default-rtdb.firebaseio.com",
  "firebaseEmail":     "rpi-node-01@yourdomain.com",
  "firebasePass":      "DevicePassword",
  "dhtPin":      4,
  "ldrSpiChannel": 0,
  "relayPin":    17,
  "ledPin":      27,
  "authButtonPin": 22,
  "irisMatchThreshold":  0.30,
  "irisEnrollFrames":    5,
  "authDisplayMs":       3000,
  "anomalyScoreThreshold": 0.60,
  "alertCooldownMs":     30000,
  "sensorIntervalMs":    10000,
  "heartbeatIntervalMs": 30000,
  "mlRiskThreshold":     0.65,
  "awsEnabled":          true,
  "awsEndpoint":         "xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com",
  "awsThingName":        "rpi_node_01",
  "awsCaPath":   "data/root-ca.crt",
  "awsCertPath": "data/device.crt",
  "awsKeyPath":  "data/device.key",
  "tfliteModelPath": "data/ambient_model.tflite"
}
```

Then:
```bash
cd firmware/rpi_sensor_node
pip install -r requirements.txt
python main.py
```

---

## Part 4 — ML Model

### 13. Train and Deploy the Ambient Model

```bash
cd models
pip install -r requirements.txt
python mock_data_generator.py --samples 5000
python model_training.py --epochs 100

# ESP32 path
python model_converter.py
# → writes firmware/esp32_sensor_node/tinyml_model.h (re-flash firmware)

# RPi path
cp model.tflite ../firmware/rpi_sensor_node/data/ambient_model.tflite
# → restart main.py
```

---

## Part 5 — Dashboard

### 14. Open the Dashboard

```bash
# Edit frontend/app.js → replace FIREBASE_CONFIG with your values
# Option 1: open directly
open frontend/index.html

# Option 2: serve locally
cd frontend && python -m http.server 8080

# Option 3: deploy to Firebase Hosting
firebase deploy --only hosting
```

---

## Part 6 — Enroll Users

1. Open the dashboard → **Enrolled Users → Enroll New User**
2. Select the target device from the dropdown
3. Enter a User ID (no spaces) and Full Name
4. Click **Send Enrollment Command**
5. The user looks at the camera for ~1 second
6. On success: template saved to device storage; entry appears under `/users/{userId}/` in Firebase

---

## Verify: Expected Serial/Log Output

**ESP32** (Serial Monitor, 115200 baud):
```
=== Iris Biometric Access Control — Boot ===
[CFG] Loaded config for device 'esp32_node_01' @ 'Entrance A'
[CAM] Ready
[BIO] Ready — 0 user(s) enrolled
[WiFi] Connected — IP: 192.168.1.42  RSSI: -62 dBm
[FB]  Connected
[AWS] Connected to xxxxxx-ats.iot.us-east-1.amazonaws.com
[FSM] INIT → CONNECTING  (0 ms in prev state)
[FSM] READY → MONITORING (3210 ms in prev state)
[BOOT] Device 'esp32_node_01' ready — 0 user(s) enrolled
```

**Raspberry Pi** (terminal):
```
=== Iris Biometric Access Control — Boot (Raspberry Pi) ===
[CFG] Loaded data/config.json
[CAM] picamera2 ready (320×240 greyscale)
[BIO] Ready — 0 user(s) enrolled
[SENS] DHT22 on GPIO 4
[SENS] MCP3008 LDR on SPI ch0
[ML]  Loaded 'data/ambient_model.tflite'
[FB]  Connected — https://yourproject-default-rtdb.firebaseio.com
[AWS] Connected to xxxxxx-ats.iot.us-east-1.amazonaws.com
[FSM] INIT → CONNECTING  (0 ms in prev state)
[FSM] READY → MONITORING (1820 ms in prev state)
[BOOT] Device 'rpi_node_01' ready — 0 user(s) enrolled
```

---

## Quick Troubleshooting

| Error | Fix |
|---|---|
| `auth/invalid-api-key` | Wrong `firebaseApiKey` in config.json |
| `auth/user-not-found` | Create the device user in Firebase Console → Auth |
| `Permission denied` | Use dev.rules during development |
| `[CAM] Init failed` (ESP32) | Use AI Thinker board + "Huge APP" partition |
| `[CAM] No camera backend found` (RPi) | Enable camera interface (`raspi-config`); install `picamera2` |
| `[BIO] SPIFFS mount failed` | Correct partition scheme; re-upload SPIFFS data |
| `[AWS] MQTT connect failed` | Verify endpoint, certs, and policy |
| Match score > 0.4 always | Add IR illumination; re-enroll; check `irisMatchThreshold` |

See `docs/TROUBLESHOOTING.md` for the full guide.
