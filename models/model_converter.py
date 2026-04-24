"""
model_converter.py
──────────────────
Converts model.tflite → tinyml_model.h (C byte array for ESP32 firmware).

Usage:
  python model_converter.py
  python model_converter.py --input model.tflite --output ../firmware/esp32_sensor_node/tinyml_model.h

What it does:
  1. Read model.tflite binary
  2. Emit a C header file with:
     - alignas(8) byte array (required by TFLite Micro)
     - Length constant
     - Metadata comment block (size, date, model info)
  3. Optionally verify the output using the tflite Python runtime
"""

import argparse
import datetime
from pathlib import Path


HEADER_TEMPLATE = """\
#pragma once
/*
 * tinyml_model.h  —  AUTO-GENERATED  —  DO NOT EDIT BY HAND
 *
 * Source:      {source}
 * Generated:   {date}
 * Model size:  {size_bytes} bytes ({size_kb:.1f} KB)
 *
 * Architecture: 3-layer MLP
 *   Input(3) → Dense(16, ReLU) → Dense(8, ReLU) → Dense(3, Softmax)
 *
 * To regenerate:
 *   python models/model_training.py
 *   python models/model_converter.py
 */

alignas(8) const unsigned char g_model[] = {{
{array_body}
}};

const unsigned int g_model_len = {size_bytes}u;
"""


def bytes_to_c_array(data: bytes, cols: int = 12) -> str:
    """Format bytes as C hex literals, `cols` per line."""
    lines = []
    for i in range(0, len(data), cols):
        chunk  = data[i : i + cols]
        hex_   = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"  {hex_},")
    # Remove trailing comma from last line (optional, some compilers warn)
    if lines:
        lines[-1] = lines[-1].rstrip(",")
    return "\n".join(lines)


def verify_model(path: Path) -> bool:
    """Quick sanity check using TFLite Python interpreter."""
    try:
        import tensorflow as tf
        interp = tf.lite.Interpreter(model_path=str(path))
        interp.allocate_tensors()
        in_det  = interp.get_input_details()
        out_det = interp.get_output_details()
        assert in_det[0]["shape"][1]  == 3, "Expected 3 input features"
        assert out_det[0]["shape"][1] == 3, "Expected 3 output classes"
        print(f"[VERIFY] OK — input shape {in_det[0]['shape']}, "
              f"output shape {out_det[0]['shape']}")
        return True
    except ImportError:
        print("[VERIFY] Skipped (TensorFlow not installed)")
        return True
    except Exception as exc:
        print(f"[VERIFY] FAILED: {exc}")
        return False


def main(input_path: str, output_path: str, verify: bool) -> None:
    src = Path(input_path)
    dst = Path(output_path)

    if not src.exists():
        print(f"[ERROR] {src} not found. Run model_training.py first.")
        raise SystemExit(1)

    data = src.read_bytes()
    print(f"[CONV] Read {len(data)} bytes from {src}")

    if verify:
        verify_model(src)

    header = HEADER_TEMPLATE.format(
        source     = src.resolve(),
        date       = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        size_bytes = len(data),
        size_kb    = len(data) / 1024,
        array_body = bytes_to_c_array(data),
    )

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(header, encoding="utf-8")
    print(f"[CONV] Written → {dst}")
    print("[DONE] Reflash the ESP32 to deploy the new model.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="TFLite → C header converter")
    parser.add_argument(
        "--input",  default="model.tflite",
        help="Path to .tflite model file")
    parser.add_argument(
        "--output", default="../firmware/esp32_sensor_node/tinyml_model.h",
        help="Destination .h file")
    parser.add_argument(
        "--no-verify", dest="verify", action="store_false",
        help="Skip TFLite Python runtime verification")
    parser.set_defaults(verify=True)
    args = parser.parse_args()
    main(args.input, args.output, args.verify)
