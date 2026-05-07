# Models — Ambient Risk ML Training Pipeline

Three-step pipeline to produce the TFLite model used on both edge platforms:

```
mock_data_generator.py  →  model_training.py  →  model_converter.py
       CSV data               model.tflite      tinyml_model.h (ESP32 only)
```

The `.tflite` binary is used directly on Raspberry Pi. The C header conversion is only needed for the ESP32-CAM.

## Tech Stack

| Tool | Version | Purpose |
|---|---|---|
| Python | 3.9+ | Training pipeline language |
| TensorFlow / Keras | 2.x | Model definition and training |
| NumPy | 1.23+ | Data processing |
| pandas | 1.5+ | CSV loading |
| scikit-learn | 1.x | Train/test split, label encoding |

## Quick Start

```bash
cd models
pip install -r requirements.txt

# Step 1 — generate synthetic training data (skip if you have real sensor data)
python mock_data_generator.py --samples 5000

# Step 2 — train MLP → export model.tflite
python model_training.py --epochs 100

# Step 3a — convert for ESP32 (produces a C byte-array header)
python model_converter.py
# → writes firmware/esp32_sensor_node/tinyml_model.h

# Step 3b — copy .tflite for Raspberry Pi
cp model.tflite ../firmware/rpi_sensor_node/data/ambient_model.tflite
```

After running step 3, flash the ESP32 firmware or restart the RPi service.

## Model Architecture

```
Input(3)  →  Dense(16, ReLU)  →  Dense(8, ReLU)  →  Dense(3, Softmax)
~700 parameters  |  ~4.2 KB TFLite  |  8 KB tensor arena on ESP32
```

- **Input**: `[temp_norm, hum_norm, light_norm]` — all normalised to [0, 1]
- **Output**: `[p_normal, p_warning, p_critical]` — softmax probabilities
- **Risk score** used by firmware: `p_warning × 0.5 + p_critical × 1.0`

### Input Normalisation

| Feature | Formula | Range |
|---|---|---|
| temperature_c | `(t − (−10)) / (60 − (−10))` | -10 °C … 60 °C |
| humidity_pct | `h / 100.0` | 0 … 100 % |
| light_norm | already normalised by ADC | 0.0 … 1.0 |

## Training on Real Data

Replace `mock_dataset.csv` with real sensor readings (same column schema):

```csv
temperature_c,humidity_pct,light_norm,label
25.3,52.1,0.78,normal
38.7,81.2,0.32,warning
47.1,91.5,0.15,critical
```

```bash
python model_training.py --data your_real_data.csv --epochs 150
python model_converter.py
cp model.tflite ../firmware/rpi_sensor_node/data/ambient_model.tflite
```

## INT8 Quantization (optional)

Reduces model size ~4× and speeds up inference on both platforms.

```bash
python model_training.py --quantize
```

- **ESP32**: update `MLInference.h` — change `_inputTensor->data.f` to `_inputTensor->data.int8` and apply the scale/offset printed by the training script
- **RPi**: `tflite-runtime` handles INT8 models automatically; no code changes needed

## Deployment Checklist

| Step | ESP32 | Raspberry Pi |
|---|---|---|
| 1. `python model_training.py` | ✓ | ✓ |
| 2. `python model_converter.py` | ✓ | — |
| 3. `cp model.tflite ../firmware/rpi_sensor_node/data/ambient_model.tflite` | — | ✓ |
| 4. Recompile and flash firmware | ✓ | — |
| 5. Restart `main.py` / systemd service | — | ✓ |
| 6. Confirm `[ML] Model loaded` in logs | ✓ | ✓ |
