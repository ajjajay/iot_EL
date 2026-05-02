"""
ml_model.py — Ambient Environmental Risk Inference
===================================================
Loads a TFLite model if available, otherwise falls back to the same
threshold rules used to label mock_dataset.csv (so predictions are
always consistent with the training data definition).

Label mapping (mirrors mock_data_generator.py and MLInference.h):
  normal   → temp < 30°C  AND humidity < 70%  AND light > 0.3
  warning  → 30 ≤ temp < 40  OR  70 ≤ humidity < 85
  critical → temp ≥ 40  OR  humidity ≥ 85
"""

import os
import numpy as np
from pathlib import Path

# ── Constants matching MLInference.h normalisation ────────────────────────────
TEMP_MIN, TEMP_MAX = -10.0, 60.0
LABEL_NAMES  = ["normal", "warning", "critical"]
LABEL_SCORES = [0.10,     0.50,      0.92]   # representative risk scores per label

# Default path — can be overridden via MODEL_PATH env var
DEFAULT_MODEL_PATH = Path(__file__).parent.parent / "models" / "model.tflite"


class AmbientRiskModel:
    """
    Wraps TFLite inference with a threshold-rules fallback.
    Instantiate once at startup; call `.predict()` per request.
    """

    def __init__(self, model_path: Path | None = None) -> None:
        self._interpreter = None
        self._input_idx   = None
        self._output_idx  = None

        path = Path(os.getenv("MODEL_PATH", str(model_path or DEFAULT_MODEL_PATH)))
        self.backend = self._try_load_tflite(path)

    # ── Public ─────────────────────────────────────────────────────────────────

    def predict(self, temperature_c: float, humidity_pct: float, light_norm: float) -> dict:
        """
        Returns:
            {
              "risk_score": float,   # 0.0 – 1.0
              "label":      str,     # "normal" | "warning" | "critical"
              "label_int":  int,     # 0 | 1 | 2
              "backend":    str,     # "tflite" | "threshold_rules"
            }
        """
        if self._interpreter is not None:
            return self._predict_tflite(temperature_c, humidity_pct, light_norm)
        return self._predict_threshold(temperature_c, humidity_pct, light_norm)

    # ── TFLite path ────────────────────────────────────────────────────────────

    def _try_load_tflite(self, path: Path) -> str:
        """Attempt to load the TFLite model. Returns the backend name."""
        if not path.exists():
            print(f"[MODEL] {path} not found — using threshold rules")
            return "threshold_rules"

        try:
            import tflite_runtime.interpreter as tflite  # type: ignore
            self._interpreter = tflite.Interpreter(model_path=str(path))
        except ImportError:
            try:
                import tensorflow as tf  # type: ignore
                self._interpreter = tf.lite.Interpreter(model_path=str(path))
            except ImportError:
                print("[MODEL] Neither tflite_runtime nor tensorflow found — using threshold rules")
                return "threshold_rules"

        self._interpreter.allocate_tensors()
        details          = self._interpreter.get_input_details()
        self._input_idx  = details[0]["index"]
        self._output_idx = self._interpreter.get_output_details()[0]["index"]
        print(f"[MODEL] TFLite model loaded from {path}")
        return "tflite"

    def _predict_tflite(self, temperature_c: float, humidity_pct: float, light_norm: float) -> dict:
        # Normalise exactly as MLInference.h does
        temp_norm = float(np.clip((temperature_c - TEMP_MIN) / (TEMP_MAX - TEMP_MIN), 0, 1))
        hum_norm  = float(np.clip(humidity_pct / 100.0, 0, 1))
        lt_norm   = float(np.clip(light_norm, 0, 1))

        inp = np.array([[temp_norm, hum_norm, lt_norm]], dtype=np.float32)
        self._interpreter.set_tensor(self._input_idx, inp)
        self._interpreter.invoke()
        probs = self._interpreter.get_tensor(self._output_idx)[0]   # shape (3,)

        label_int  = int(np.argmax(probs))
        risk_score = float(probs[1] * 0.5 + probs[2] * 0.92)       # weighted by severity
        risk_score = min(risk_score, 1.0)

        return {
            "risk_score": round(risk_score, 4),
            "label":      LABEL_NAMES[label_int],
            "label_int":  label_int,
            "backend":    "tflite",
        }

    # ── Threshold-rules fallback (mirrors mock_data_generator.label()) ─────────

    def _predict_threshold(self, temperature_c: float, humidity_pct: float, light_norm: float) -> dict:
        """
        Replicates the exact label() function from mock_data_generator.py.
        Uses a soft risk score derived from how far the reading is from each boundary.
        """
        # Determine label
        if temperature_c >= 40.0 or humidity_pct >= 85.0:
            label_int = 2  # critical
        elif temperature_c >= 30.0 or humidity_pct >= 70.0:
            label_int = 1  # warning
        else:
            label_int = 0  # normal

        # Soft risk score: interpolate within each region for smoother values
        if label_int == 0:
            # normal → 0.0 – 0.25
            t_factor = max(temperature_c - 20.0, 0) / 10.0   # 0 at 20°C, 1 at 30°C
            h_factor = max(humidity_pct  - 50.0, 0) / 20.0   # 0 at 50%, 1 at 70%
            risk_score = 0.05 + 0.20 * max(t_factor, h_factor)
        elif label_int == 1:
            # warning → 0.25 – 0.70
            t_factor = (temperature_c - 30.0) / 10.0         # 0 at 30°C, 1 at 40°C
            h_factor = (humidity_pct  - 70.0) / 15.0         # 0 at 70%, 1 at 85%
            risk_score = 0.30 + 0.40 * max(
                min(max(t_factor, 0), 1),
                min(max(h_factor, 0), 1),
            )
        else:
            # critical → 0.75 – 1.0
            t_factor = (temperature_c - 40.0) / 20.0         # 0 at 40°C, 1 at 60°C
            h_factor = (humidity_pct  - 85.0) / 15.0         # 0 at 85%, 1 at 100%
            risk_score = 0.78 + 0.22 * max(
                min(max(t_factor, 0), 1),
                min(max(h_factor, 0), 1),
            )

        # Low light nudges risk up slightly (dark + hot = suspicious)
        if light_norm < 0.3 and label_int > 0:
            risk_score = min(risk_score + 0.05, 1.0)

        return {
            "risk_score": round(float(risk_score), 4),
            "label":      LABEL_NAMES[label_int],
            "label_int":  label_int,
            "backend":    "threshold_rules",
        }
