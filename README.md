# IoT Smart Monitor

A production-ready IoT monitoring and control system with TinyML inference on ESP32, Firebase Realtime Database, and a real-time web dashboard.

## What's Inside

| Layer | Technology | Description |
|---|---|---|
| Firmware | C++ / Arduino (ESP32) | Sensor reading, FSM, TFLite Micro inference, Firebase sync |
| ML | Python / TensorFlow → TFLite | 3-class risk classifier (normal / warning / critical) |
| Backend | Firebase RTDB | Real-time push/pull, offline queue, device commands |
| Dashboard | HTML5 / Chart.js | Live charts, device cards, alerts, manual controls |

## Project Structure

```
IoT_EL/
├── .gitignore
├── .env.example              ← copy to .env, fill credentials
├── README.md
├── claude.md                 ← comprehensive system documentation
│
├── firmware/
│   ├── README.md
│   └── esp32_sensor_node/
│       ├── esp32_sensor_node.ino    ← main entry point
│       ├── StateManager.h/.cpp      ← FSM (INIT→CONNECTING→READY→MONITORING⇄ALERT)
│       ├── SensorManager.h/.cpp     ← DHT22 + LDR + EMA smoothing
│       ├── ActuatorController.h/.cpp← relay + LED patterns + override expiry
│       ├── FirebaseManager.h/.cpp   ← push/pull + offline ring buffer
│       ├── MLInference.h/.cpp       ← TFLite Micro wrapper
│       ├── ConfigManager.h/.cpp     ← SPIFFS JSON config
│       └── tinyml_model.h           ← embedded model byte array
│
├── models/
│   ├── README.md
│   ├── requirements.txt
│   ├── mock_data_generator.py       ← generate training CSV
│   ├── model_training.py            ← train MLP → model.tflite
│   └── model_converter.py           ← tflite → tinyml_model.h
│
├── firebase/
│   ├── README.md
│   ├── schema.json                  ← database shape reference
│   ├── dev.rules                    ← open rules for development
│   └── prod.rules                   ← least-privilege production rules
│
├── frontend/
│   ├── README.md
│   ├── index.html                   ← dashboard layout
│   ├── styles.css                   ← dark/light theme, responsive
│   └── app.js                       ← Firebase integration, charts, controls
│
├── scripts/
│   ├── setup.sh                     ← one-shot environment setup
│   └── firebase_setup.md            ← step-by-step Firebase guide
│
└── docs/
    ├── ARCHITECTURE.md
    ├── TROUBLESHOOTING.md
    └── API_REFERENCE.md
```

## Quick Start

```bash
# 1. Clone
git clone https://github.com/ajjajay/iot_EL.git
cd iot_EL

# 2. Setup Python ML environment
bash scripts/setup.sh

# 3. Train model → generate tinyml_model.h
cd models/
python model_training.py
python model_converter.py
cd ..

# 4. Edit firmware config
# → firmware/esp32_sensor_node/data/config.json  (credentials)

# 5. Flash ESP32 (Arduino IDE or PlatformIO)

# 6. Open dashboard
# → frontend/index.html (edit FIREBASE_CONFIG in app.js first)
```

See `claude.md` for the complete guide.

## License

MIT
