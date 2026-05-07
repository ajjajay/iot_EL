import os
import time
import numpy as np
from dataclasses import dataclass, field
from typing import Optional

IRIS_FEAT_DIM  = 64
IRIS_GRID_SIDE = 8
SENSOR_MOCK    = os.environ.get("SENSOR_MOCK", "0") == "1"


@dataclass
class IrisCapture:
    features: np.ndarray = field(default_factory=lambda: np.zeros(IRIS_FEAT_DIM, dtype=np.float32))
    valid: bool = False
    timestamp_ms: float = 0.0


class IrisCamera:
    def __init__(self):
        self._camera  = None
        self._backend = None   # "picamera2" | "opencv" | "mock"
        self._ready   = False

    def begin(self) -> bool:
        if SENSOR_MOCK:
            self._backend = "mock"
            self._ready   = True
            print("[CAM] Mock mode active")
            return True

        # Prefer picamera2 (Raspberry Pi Camera Module v2/v3)
        try:
            from picamera2 import Picamera2
            cam = Picamera2()
            cfg = cam.create_still_configuration(
                main={"size": (320, 240), "format": "L"}   # L = 8-bit greyscale
            )
            cam.configure(cfg)
            cam.start()
            self._camera  = cam
            self._backend = "picamera2"
            self._ready   = True
            print("[CAM] picamera2 ready (320×240 greyscale)")
            return True
        except Exception as e:
            print(f"[CAM] picamera2 unavailable: {e}")

        # Fall back to OpenCV (USB webcam on /dev/video0)
        try:
            import cv2
            cap = cv2.VideoCapture(0)
            cap.set(cv2.CAP_PROP_FRAME_WIDTH,  320)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 240)
            if not cap.isOpened():
                raise RuntimeError("Cannot open /dev/video0")
            self._camera  = cap
            self._backend = "opencv"
            self._ready   = True
            print("[CAM] OpenCV webcam ready (320×240)")
            return True
        except Exception as e:
            print(f"[CAM] OpenCV unavailable: {e}")

        print("[CAM] No camera backend found")
        return False

    def capture(self) -> IrisCapture:
        r = IrisCapture(timestamp_ms=time.time() * 1000)
        if not self._ready:
            print("[CAM] capture() called before begin()")
            return r
        gray = self._grab_gray()
        if gray is None:
            print("[CAM] Frame grab failed")
            return r
        r.features = self._extract_features(gray)
        r.valid    = True
        return r

    def capture_average(self, num_frames: int = 5, delay_ms: int = 200) -> IrisCapture:
        avg = IrisCapture(timestamp_ms=time.time() * 1000)
        acc = np.zeros(IRIS_FEAT_DIM, dtype=np.float64)
        valid_count = 0
        for i in range(num_frames):
            c = self.capture()
            if c.valid:
                acc += c.features
                valid_count += 1
            if i < num_frames - 1:
                time.sleep(delay_ms / 1000)
        if valid_count == 0:
            return avg
        avg.features = (acc / valid_count).astype(np.float32)
        avg.valid    = True
        print(f"[CAM] Averaged {valid_count}/{num_frames} frames")
        return avg

    def is_ready(self) -> bool:
        return self._ready

    def close(self):
        if self._backend == "picamera2" and self._camera:
            self._camera.stop()
        elif self._backend == "opencv" and self._camera:
            self._camera.release()

    # ── Private ───────────────────────────────────────────────────────────────

    def _grab_gray(self) -> Optional[np.ndarray]:
        if self._backend == "mock":
            # Stable random iris pattern seeded on wall-clock second so consecutive
            # frames within 1 s look identical (simulates a real iris being held steady)
            rng = np.random.default_rng(int(time.time()))
            return rng.integers(40, 220, (240, 320), dtype=np.uint8)

        if self._backend == "picamera2":
            try:
                frame = self._camera.capture_array()
                if frame.ndim == 3:
                    import cv2
                    frame = cv2.cvtColor(frame, cv2.COLOR_RGB2GRAY)
                return frame
            except Exception as e:
                print(f"[CAM] picamera2 grab error: {e}")
                return None

        if self._backend == "opencv":
            try:
                import cv2
                ret, frame = self._camera.read()
                if not ret:
                    return None
                return cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            except Exception as e:
                print(f"[CAM] OpenCV grab error: {e}")
                return None

        return None

    def _extract_features(self, gray: np.ndarray) -> np.ndarray:
        """8×8 zonal mean-intensity descriptor, normalised to [0, 1] — mirrors _extractFeatures() in IrisCamera.cpp."""
        h, w    = gray.shape
        cell_w  = w // IRIS_GRID_SIDE
        cell_h  = h // IRIS_GRID_SIDE
        features = np.zeros(IRIS_FEAT_DIM, dtype=np.float32)
        for gy in range(IRIS_GRID_SIDE):
            for gx in range(IRIS_GRID_SIDE):
                y0, y1 = gy * cell_h, min((gy + 1) * cell_h, h)
                x0, x1 = gx * cell_w, min((gx + 1) * cell_w, w)
                features[gy * IRIS_GRID_SIDE + gx] = gray[y0:y1, x0:x1].mean() / 255.0
        return features
