"""
mock_data_generator.py
──────────────────────
Generates a labelled CSV dataset of synthetic sensor readings.
Used to train the TinyML model without real hardware.

Label logic (mirrors the ESP32 threshold rules):
  normal   → temp < 30°C  AND humidity < 70%  AND light > 0.3
  warning  → 30 ≤ temp < 40  OR  70 ≤ humidity < 85
  critical → temp ≥ 40  OR  humidity ≥ 85

Output: mock_dataset.csv
Columns: temperature_c, humidity_pct, light_norm, label
"""

import csv
import random
import math
import argparse
from pathlib import Path


def label(temp: float, hum: float, light: float) -> str:
    if temp >= 40.0 or hum >= 85.0:
        return "critical"
    if temp >= 30.0 or hum >= 70.0:
        return "warning"
    return "normal"


def generate_sample(state: str) -> tuple[float, float, float, str]:
    """Generate a realistic sensor sample for the given target state."""
    if state == "normal":
        temp  = random.gauss(22.0, 4.0)
        hum   = random.gauss(50.0, 10.0)
        light = random.uniform(0.35, 1.0)
    elif state == "warning":
        temp  = random.gauss(34.0, 3.0)
        hum   = random.gauss(75.0, 5.0)
        light = random.uniform(0.1, 0.6)
    else:  # critical
        temp  = random.gauss(46.0, 4.0)
        hum   = random.gauss(90.0, 5.0)
        light = random.uniform(0.0, 0.4)

    # Clamp to physical limits
    temp  = max(-10.0, min(60.0, temp))
    hum   = max(0.0,   min(100.0, hum))
    light = max(0.0,   min(1.0,   light))

    # Re-derive label from values (ensures consistency with threshold rules)
    actual_label = label(temp, hum, light)
    return round(temp, 2), round(hum, 2), round(light, 4), actual_label


def main(n_samples: int = 5000, output: str = "mock_dataset.csv") -> None:
    random.seed(42)
    outpath = Path(__file__).parent / output

    # Balanced class distribution: 50% normal, 30% warning, 20% critical
    state_dist = (
        ["normal"]   * int(n_samples * 0.50) +
        ["warning"]  * int(n_samples * 0.30) +
        ["critical"] * int(n_samples * 0.20)
    )
    random.shuffle(state_dist)

    counts = {"normal": 0, "warning": 0, "critical": 0}

    with open(outpath, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["temperature_c", "humidity_pct", "light_norm", "label"])
        for target in state_dist:
            temp, hum, light, actual_label = generate_sample(target)
            writer.writerow([temp, hum, light, actual_label])
            counts[actual_label] += 1

    print(f"Generated {n_samples} samples → {outpath}")
    print(f"  normal: {counts['normal']}, warning: {counts['warning']}, "
          f"critical: {counts['critical']}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate IoT mock sensor dataset")
    parser.add_argument("--samples", type=int, default=5000)
    parser.add_argument("--output",  type=str, default="mock_dataset.csv")
    args = parser.parse_args()
    main(args.samples, args.output)
