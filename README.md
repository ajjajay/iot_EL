# Iris Biometric Access Control System

A production-grade IoT biometric access control system with iris recognition on the edge, Firebase Realtime Database, AWS IoT Core + Bedrock AI alerting, and a real-time web dashboard.

## Tech Stack

| Layer | Technology | Details |
|---|---|---|
| **Edge — ESP32** | C++ / Arduino (AI Thinker ESP32-CAM) | OV2640 iris capture, 64-element feature extraction, TFLite Micro, Firebase sync, MQTT/TLS to AWS |
| **Edge — RPi** | Python 3.10+ | Raspberry Pi Camera Module / USB webcam, same biometric pipeline, TFLite runtime, paho-mqtt |
| **Biometrics** | Normalised RMS matching | 8×8 zonal iris descriptor, sliding-window anomaly detector |
| **Cloud DB** | Firebase Realtime Database | Device state, sign-in history, enrollment registry, alerts |
| **Cloud Messaging** | AWS IoT Core → Lambda → Bedrock Agent → SNS | Anomaly alerts with AI-generated notifications |
| **ML** | Python / Keras → TFLite | 3-class ambient risk classifier (normal / warning / critical) |
| **Dashboard** | HTML5 / CSS / Chart.js | Live charts, sign-in log, enrolled users, manual door controls |
| **Backend** | Node.js / Express (Render) | REST proxy for cross-origin Firebase writes (optional) |

## Project Structure

```
iot_EL/
├── CLAUDE.md                         ← full system documentation
├── README.md
├── render.yaml                       ← Render hosting config (backend)
│
├── firmware/
│   ├── README.md
│   ├── esp32_sensor_node/            ← Arduino C++ firmware (AI Thinker ESP32-CAM)
│   │   ├── esp32_sensor_node.ino     ← main entry point / FSM orchestrator
│   │   ├── IrisCamera.h/.cpp         ← OV2640 capture + 8×8 feature extraction
│   │   ├── BiometricManager.h/.cpp   ← SPIFFS template storage, enroll/match
│   │   ├── AnomalyDetector.h/.cpp    ← sliding-window composite scorer
│   │   ├── AlertManager.h/.cpp       ← throttled AWS+Firebase alert dispatch
│   │   ├── StateManager.h/.cpp       ← FSM (10 states, legal transition table)
│   │   ├── ConfigManager.h/.cpp      ← SPIFFS JSON config + compile-time fallbacks
│   │   ├── SensorManager.h/.cpp      ← DHT22 + LDR + EMA smoothing
│   │   ├── ActuatorController.h/.cpp ← relay + LED patterns + override expiry
│   │   ├── FirebaseManager.h/.cpp    ← RTDB push/pull + offline ring buffer
│   │   ├── AWSIoTManager.h/.cpp      ← MQTT/TLS publish + subscribe + agent ACK
│   │   ├── MLInference.h/.cpp        ← TFLite Micro ambient inference
│   │   ├── aws_certificates.h        ← X.509 certs (paste from AWS console)
│   │   ├── tinyml_model.h            ← embedded model byte array (generated)
│   │   └── data/
│   │       └── config.json           ← per-device credentials (SPIFFS)
│   │
│   └── rpi_sensor_node/              ← Python 3 firmware (Raspberry Pi)
│       ├── main.py                   ← main entry point / FSM orchestrator
│       ├── iris_camera.py            ← picamera2 / OpenCV + feature extraction
│       ├── biometric_manager.py      ← filesystem template storage, enroll/match
│       ├── anomaly_detector.py       ← same sliding-window scorer as C++ version
│       ├── alert_manager.py          ← same throttled alert dispatch
│       ├── state_manager.py          ← same FSM
│       ├── config_manager.py         ← JSON config from data/config.json
│       ├── sensor_manager.py         ← DHT22 (adafruit-dht) + MCP3008 LDR
│       ├── actuator_controller.py    ← RPi.GPIO relay + LED
│       ├── firebase_manager.py       ← Firebase REST API + offline queue
│       ├── aws_iot_manager.py        ← paho-mqtt MQTT/TLS
│       ├── ml_inference.py           ← tflite-runtime ambient inference
│       ├── requirements.txt
│       └── data/
│           └── config.json           ← per-device credentials
│
├── models/
│   ├── README.md
│   ├── requirements.txt
│   ├── mock_data_generator.py        ← generate 5000-sample training CSV
│   ├── model_training.py             ← train MLP → model.tflite
│   ├── model_converter.py            ← tflite → tinyml_model.h (ESP32 only)
│   └── mock_dataset.csv              ← pre-generated training data
│
├── firebase/
│   ├── README.md
│   ├── dev.rules                     ← open rules for development
│   └── prod.rules                    ← least-privilege production rules
│
├── frontend/
│   ├── README.md
│   ├── index.html                    ← dashboard layout
│   ├── styles.css                    ← dark/light theme, responsive grid
│   └── app.js                        ← Firebase listener, charts, controls
│
├── backend/
│   ├── server.js                     ← Express REST proxy
│   └── package.json
│
├── docs/
│   ├── ARCHITECTURE.md
│   ├── TROUBLESHOOTING.md
│   └── API_REFERENCE.md
│
└── scripts/
    └── firebase_setup.md             ← step-by-step Firebase + AWS setup guide
```

---

## Quick Start — ESP32-CAM Node

### Prerequisites
- Arduino IDE 2.x with ESP32 board support
- AI Thinker ESP32-CAM board
- ESP32 SPIFFS Uploader plugin

```bash
# 1. Clone
git clone https://github.com/ajjajay/iot_EL.git
cd iot_EL

# 2. Train ambient ML model → generates tinyml_model.h
cd models
pip install -r requirements.txt
python mock_data_generator.py
python model_training.py --epochs 100
python model_converter.py          # writes firmware/esp32_sensor_node/tinyml_model.h
cd ..

# 3. Configure credentials
#    Edit firmware/esp32_sensor_node/data/config.json
#    (Firebase keys, AWS endpoint, WiFi, device ID)

# 4. Paste AWS X.509 certs into firmware/esp32_sensor_node/aws_certificates.h

# 5. Arduino IDE
#    Tools → Board → AI Thinker ESP32-CAM
#    Tools → Partition Scheme → Huge APP (3 MB app / 1 MB SPIFFS)
#    Tools → ESP32 Sketch Data Upload   ← uploads config.json to SPIFFS
#    Sketch → Upload                    ← flash firmware

# 6. Open dashboard
#    Edit frontend/app.js → replace FIREBASE_CONFIG with your values
#    Open frontend/index.html in a browser
```

---

## Quick Start — Raspberry Pi Node

### Prerequisites
- Raspberry Pi 3/4/5 with Raspberry Pi OS (64-bit recommended)
- Raspberry Pi Camera Module v2/v3 **or** USB webcam
- MCP3008 SPI ADC chip (for LDR/light sensor — RPi has no onboard ADC)

```bash
# 1. On the Raspberry Pi
git clone https://github.com/ajjajay/iot_EL.git
cd iot_EL

# 2. Install Python dependencies
cd firmware/rpi_sensor_node
pip install -r requirements.txt

# 3. Build the ambient TFLite model
cd ../../models
pip install -r requirements.txt
python mock_data_generator.py
python model_training.py --epochs 100
# Copy the .tflite file (NOT the C header) to the RPi node
cp model.tflite ../firmware/rpi_sensor_node/data/ambient_model.tflite
cd ../firmware/rpi_sensor_node

# 4. Copy AWS certificates into data/
#    data/root-ca.crt   ← Amazon root CA
#    data/device.crt    ← device certificate
#    data/device.key    ← private key

# 5. Edit data/config.json with your credentials
#    (Firebase keys, AWS endpoint, GPIO pin numbers, device ID)

# 6. Enable SPI and camera interfaces
sudo raspi-config
# → Interface Options → Camera → Enable
# → Interface Options → SPI → Enable

# 7. Run
python main.py

# Run in demo mode (no hardware needed)
SENSOR_MOCK=1 python main.py
```

---

## Enrolling a User

1. Open the dashboard → **Enrolled Users** → **Enroll New User**
2. Select device, enter User ID and Full Name, click **Send Enrollment Command**
3. The device enters ENROLLING state — the user looks at the camera for ~1 second
4. Template saved to device storage; enrollment record pushed to Firebase `/users/`

Repeat for each user. Up to 16 users × 5 templates per device.

---

## Running the Backend (optional)

The Express backend acts as a REST proxy for environments where the dashboard needs server-side Firebase credentials.

```bash
cd backend
npm install
# Set environment variables (FIREBASE_API_KEY, etc.)
npm start
```

Or deploy to Render using `render.yaml` at the repo root.

---

## License

MIT
