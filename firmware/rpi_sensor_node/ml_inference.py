import os
import numpy as np
from dataclasses import dataclass

_TEMP_MIN, _TEMP_MAX = -10.0, 60.0
_HUM_MIN,  _HUM_MAX  =   0.0, 100.0


@dataclass
class MLResult:
    p_normal: float   = 0.0
    p_warning: float  = 0.0
    p_critical: float = 0.0
    risk_score: float = 0.0   # p_warning * 0.5 + p_critical * 1.0
    label: int        = 0     # 0=normal 1=warning 2=critical
    valid: bool       = False


class MLInference:
    def __init__(self, model_path: str = "data/ambient_model.tflite"):
        self._model_path   = model_path
        self._interpreter  = None
        self._ready        = False

    def begin(self) -> bool:
        if not os.path.exists(self._model_path):
            print(f"[ML] Model not found at '{self._model_path}' — env inference disabled")
            return False
        try:
            # Prefer the lightweight tflite-runtime package
            import tflite_runtime.interpreter as tflite
            self._interpreter = tflite.Interpreter(model_path=self._model_path)
        except ImportError:
            try:
                import tensorflow as tf
                self._interpreter = tf.lite.Interpreter(model_path=self._model_path)
            except Exception as e:
                print(f"[ML] TFLite not available: {e}")
                return False
        except Exception as e:
            print(f"[ML] Model load failed: {e}")
            return False

        self._interpreter.allocate_tensors()
        self._inp = self._interpreter.get_input_details()
        self._out = self._interpreter.get_output_details()
        self._ready = True
        print(f"[ML] Loaded '{self._model_path}'")
        return True

    def infer(self, temp_c: float, hum_pct: float, light_norm: float) -> MLResult:
        r = MLResult()
        if not self._ready:
            return r
        try:
            x = np.array([[
                self._norm(temp_c,    _TEMP_MIN, _TEMP_MAX),
                self._norm(hum_pct,   _HUM_MIN,  _HUM_MAX),
                float(light_norm),
            ]], dtype=np.float32)
            self._interpreter.set_tensor(self._inp[0]["index"], x)
            self._interpreter.invoke()
            probs = self._interpreter.get_tensor(self._out[0]["index"])[0]
            r.p_normal   = float(probs[0])
            r.p_warning  = float(probs[1])
            r.p_critical = float(probs[2])
            r.risk_score = r.p_warning * 0.5 + r.p_critical * 1.0
            r.label      = int(np.argmax(probs))
            r.valid      = True
        except Exception as e:
            print(f"[ML] Inference error: {e}")
        return r

    @staticmethod
    def _norm(val: float, mn: float, mx: float) -> float:
        return max(0.0, min(1.0, (val - mn) / (mx - mn)))
