"""
model_training.py
──────────────────
Trains a small MLP classifier on sensor data, exports as TFLite model.

Pipeline:
  1. Generate (or load) mock_dataset.csv
  2. Preprocess: normalise features, one-hot encode labels
  3. Train a 3-layer MLP with Keras
  4. Convert to TFLite (float32, no quantization for first pass)
  5. Save model.tflite → ready for model_converter.py

Usage:
  python model_training.py                   # train on generated data
  python model_training.py --data real.csv  # train on real sensor CSV
  python model_training.py --epochs 50 --quantize   # INT8 quantized export

Requirements:
  pip install -r requirements.txt
"""

import argparse
import os
import numpy as np
import pandas as pd
from pathlib import Path

os.environ["TF_CPP_MIN_LOG_LEVEL"] = "2"  # suppress TF info/warnings
import tensorflow as tf
from tensorflow import keras
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import LabelEncoder, StandardScaler
from sklearn.metrics import classification_report


# ── Constants ─────────────────────────────────────────────────────────────────
LABEL_MAP = {"normal": 0, "warning": 1, "critical": 2}
FEATURE_COLS = ["temperature_c", "humidity_pct", "light_norm"]
LABEL_COL    = "label"
MODEL_DIR    = Path(__file__).parent


def load_or_generate_data(csv_path: Path) -> pd.DataFrame:
    if not csv_path.exists():
        print(f"[DATA] {csv_path} not found — generating mock data...")
        from mock_data_generator import main as gen
        gen(5000, csv_path.name)
    df = pd.read_csv(csv_path)
    print(f"[DATA] Loaded {len(df)} samples from {csv_path}")
    print(df["label"].value_counts().to_string())
    return df


def preprocess(df: pd.DataFrame):
    X = df[FEATURE_COLS].values.astype(np.float32)
    y_raw = df[LABEL_COL].map(LABEL_MAP).values

    # Normalise features to [0, 1] using training set statistics
    # We hard-code the clip ranges to match MLInference.h constants
    X[:, 0] = np.clip((X[:, 0] - (-10)) / (60 - (-10)), 0, 1)  # temp
    X[:, 1] = np.clip(X[:, 1] / 100.0, 0, 1)                    # humidity
    # light_norm already 0–1

    y = keras.utils.to_categorical(y_raw, num_classes=3)
    return X, y, y_raw


def build_model() -> keras.Model:
    """
    3-layer MLP sized for ESP32 RAM:
      Input(3) → Dense(16, ReLU) → Dense(8, ReLU) → Dense(3, Softmax)
    ~700 parameters — fits in 8 KB tensor arena.
    """
    model = keras.Sequential([
        keras.layers.Input(shape=(3,)),
        keras.layers.Dense(16, activation="relu"),
        keras.layers.Dense(8,  activation="relu"),
        keras.layers.Dense(3,  activation="softmax"),
    ], name="iot_risk_classifier")
    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=1e-3),
        loss="categorical_crossentropy",
        metrics=["accuracy"],
    )
    model.summary()
    return model


def convert_to_tflite(model: keras.Model, quantize: bool,
                       repr_data: np.ndarray) -> bytes:
    converter = tf.lite.TFLiteConverter.from_keras_model(model)

    if quantize:
        def representative_dataset():
            for i in range(min(200, len(repr_data))):
                yield [repr_data[i:i+1]]
        converter.optimizations          = [tf.lite.Optimize.DEFAULT]
        converter.representative_dataset = representative_dataset
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        converter.inference_input_type   = tf.int8
        converter.inference_output_type  = tf.int8
        print("[TFLite] Converting with INT8 quantization...")
    else:
        print("[TFLite] Converting with float32 (no quantization)...")

    return converter.convert()


def main(args):
    csv_path = MODEL_DIR / args.data
    df = load_or_generate_data(csv_path)
    X, y, y_raw = preprocess(df)

    X_train, X_test, y_train, y_test, y_raw_train, y_raw_test = train_test_split(
        X, y, y_raw, test_size=0.2, random_state=42, stratify=y_raw
    )

    model = build_model()
    callbacks = [
        keras.callbacks.EarlyStopping(monitor="val_accuracy",
                                       patience=10, restore_best_weights=True),
        keras.callbacks.ReduceLROnPlateau(patience=5, factor=0.5),
    ]
    print(f"\n[TRAIN] Training for up to {args.epochs} epochs...")
    history = model.fit(
        X_train, y_train,
        validation_split=0.2,
        epochs=args.epochs,
        batch_size=32,
        callbacks=callbacks,
        verbose=1,
    )

    # Evaluation
    y_pred  = np.argmax(model.predict(X_test, verbose=0), axis=1)
    print("\n[EVAL] Classification report:")
    print(classification_report(y_raw_test, y_pred,
                                 target_names=list(LABEL_MAP.keys())))
    test_loss, test_acc = model.evaluate(X_test, y_test, verbose=0)
    print(f"[EVAL] Test accuracy: {test_acc:.4f}  Loss: {test_loss:.4f}")

    # TFLite export
    tflite_bytes = convert_to_tflite(model, args.quantize, X_train)
    tflite_path  = MODEL_DIR / "model.tflite"
    tflite_path.write_bytes(tflite_bytes)
    print(f"[SAVE] TFLite model saved → {tflite_path} ({len(tflite_bytes)} bytes)")
    print("[NEXT] Run `python model_converter.py` to generate tinyml_model.h")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Train IoT risk classifier")
    parser.add_argument("--data",     default="mock_dataset.csv")
    parser.add_argument("--epochs",   type=int, default=100)
    parser.add_argument("--quantize", action="store_true",
                        help="Export INT8 quantized model (smaller, faster)")
    main(parser.parse_args())
