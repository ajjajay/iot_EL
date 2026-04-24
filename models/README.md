# Models — TinyML Training Pipeline

## Overview

Three-step pipeline to produce the C model header embedded in firmware:

```
mock_data_generator.py  →  model_training.py  →  model_converter.py
      CSV data               model.tflite          tinyml_model.h
```

## Quick Start

```bash
cd models/
pip install -r requirements.txt

# Step 1 (optional — training.py auto-calls this if CSV missing)
python mock_data_generator.py --samples 5000

# Step 2 — train and export TFLite model
python model_training.py --epochs 100

# Step 3 — convert TFLite → C header
python model_converter.py

# Now copy firmware-side model:
cp ../firmware/esp32_sensor_node/tinyml_model.h .  # or the converter writes there directly
```

## Model Architecture

```
Input(3)  →  Dense(16, ReLU)  →  Dense(8, ReLU)  →  Dense(3, Softmax)
```

- **Input**: `[temp_norm, hum_norm, light_norm]` — all in `[0, 1]`
- **Output**: `[p_normal, p_warning, p_critical]` — softmax probabilities
- **Risk score**: `p_warning × 0.5 + p_critical × 1.0`
- **Parameters**: ~700 — fits comfortably in 8 KB ESP32 tensor arena

## Training on Real Data

Replace `mock_dataset.csv` with your own CSV (same column names):

```
temperature_c, humidity_pct, light_norm, label
25.3, 52.1, 0.78, normal
38.7, 81.2, 0.32, warning
47.1, 91.5, 0.15, critical
```

Then:
```bash
python model_training.py --data your_real_data.csv --epochs 150
python model_converter.py
```

## INT8 Quantization (optional — ~4× smaller, faster)

```bash
python model_training.py --quantize
```

After quantization, update `MLInference.h`: change `_inputTensor->data.f` to
`_inputTensor->data.int8` and apply the quantization scale/offset from
`get_input_details()`. The training script prints these values.

## Redeployment Checklist

1. `python model_training.py` — produces `model.tflite`
2. `python model_converter.py` — updates `tinyml_model.h`
3. Recompile and flash all ESP32 nodes
4. Monitor serial output for `[ML] Model loaded OK` confirmation
