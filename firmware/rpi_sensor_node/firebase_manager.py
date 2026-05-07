import collections
import threading
import time
from dataclasses import dataclass
from typing import Optional, Tuple

from sensor_manager import SensorReading
from ml_inference import MLResult
from state_manager import DeviceState
from actuator_controller import RelayState

_OFFLINE_QUEUE_SIZE = 30


@dataclass
class _QueuedReading:
    sensor: SensorReading
    ml: MLResult
    state: DeviceState


class FirebaseManager:
    """
    Firebase Realtime Database client using the REST API + Email/Password auth.
    Mirrors FirebaseManager.h/.cpp — same paths, same offline queue.
    """

    def __init__(self, api_key: str, database_url: str,
                 email: str, password: str, device_id: str):
        self._api_key      = api_key
        self._db_url       = database_url.rstrip("/")
        self._email        = email
        self._password     = password
        self._device_id    = device_id
        self._id_token     = ""
        self._refresh_token = ""
        self._token_exp    = 0.0
        self._connected    = False
        self._queue: collections.deque = collections.deque(maxlen=_OFFLINE_QUEUE_SIZE)
        self._lock         = threading.Lock()
        self._requests     = None

    def begin(self) -> bool:
        try:
            import requests as req
            self._requests = req
        except ImportError:
            print("[FB] 'requests' not installed — Firebase disabled")
            return False

        try:
            url = (f"https://identitytoolkit.googleapis.com/v1/"
                   f"accounts:signInWithPassword?key={self._api_key}")
            resp = self._requests.post(url, json={
                "email": self._email,
                "password": self._password,
                "returnSecureToken": True,
            }, timeout=10)
            if resp.status_code != 200:
                print(f"[FB] Auth failed ({resp.status_code}): {resp.text[:200]}")
                return False
            body = resp.json()
            self._id_token      = body["idToken"]
            self._refresh_token = body["refreshToken"]
            self._token_exp     = time.monotonic() + int(body.get("expiresIn", 3600))
            self._connected     = True
            print(f"[FB] Connected — {self._db_url}")
            return True
        except Exception as e:
            print(f"[FB] begin() failed: {e}")
            return False

    # ── Public API (mirrors FirebaseManager.h) ────────────────────────────────

    def push_reading(self, s: SensorReading, ml: MLResult, state: DeviceState):
        if not self._connected:
            with self._lock:
                self._queue.append(_QueuedReading(s, ml, state))
            return
        if not self._push_one(s, ml, state):
            with self._lock:
                self._queue.append(_QueuedReading(s, ml, state))

    def send_heartbeat(self, state: DeviceState):
        self._patch(f"/devices/{self._device_id}/heartbeat", {
            "ts":     int(time.time()),
            "state":  state.value,
            "uptime": int(time.monotonic()),
        })
        self._patch(f"/devices/{self._device_id}", {"online": True})

    def flush_queue(self):
        with self._lock:
            items = list(self._queue)
            self._queue.clear()
        for item in items:
            self._push_one(item.sensor, item.ml, item.state)

    def set_online(self, online: bool):
        self._patch(f"/devices/{self._device_id}", {"online": online})

    def push_sign_in(self, user_id: str, user_name: str,
                     match_score: float, success: bool, anomaly_score: float):
        self._post(f"/signins/{self._device_id}", {
            "userId":      user_id,
            "userName":    user_name,
            "deviceId":    self._device_id,
            "matchScore":  match_score,
            "success":     success,
            "anomalyScore": anomaly_score,
            "ts":          int(time.time() * 1000),
        })

    def push_enrollment(self, user_id: str, name: str):
        self._put(f"/users/{user_id}", {
            "userId":     user_id,
            "name":       name,
            "deviceId":   self._device_id,
            "enrolledAt": int(time.time() * 1000),
            "active":     True,
        })

    def push_biometric_alert(self, user_id: str, alert_type: str, anomaly_score: float):
        self._post(f"/alerts/{self._device_id}", {
            "deviceId":    self._device_id,
            "alertType":   alert_type,
            "userId":      user_id,
            "anomalyScore": anomaly_score,
            "ts":          int(time.time() * 1000),
            "acknowledged": False,
        })

    def poll_commands(self) -> Optional[RelayState]:
        val = self._get(f"/devices/{self._device_id}/commands/relayOverride")
        if val == "ON":
            return RelayState.ON
        if val == "OFF":
            return RelayState.OFF
        return None

    def poll_enroll_command(self) -> Optional[Tuple[str, str]]:
        pending = self._get(f"/devices/{self._device_id}/commands/enroll/pending")
        if not pending:
            return None
        user_id = self._get(f"/devices/{self._device_id}/commands/enroll/userId") or ""
        name    = self._get(f"/devices/{self._device_id}/commands/enroll/name")   or ""
        if user_id:
            self._patch(f"/devices/{self._device_id}/commands/enroll", {"pending": False})
            return (user_id, name)
        return None

    def is_connected(self) -> bool:
        return self._connected

    # ── Private ───────────────────────────────────────────────────────────────

    def _push_one(self, s: SensorReading, ml: MLResult, state: DeviceState) -> bool:
        now_ms = int(time.time() * 1000)
        payload = {
            "temperatureC": s.temperature_c,
            "humidityPct":  s.humidity_pct,
            "lightRaw":     s.light_raw,
            "lightNorm":    s.light_norm,
            "riskScore":    ml.risk_score,
            "mlLabel":      ml.label,
            "state":        state.value,
            "ts":           now_ms,
        }
        ok1 = self._patch(f"/devices/{self._device_id}/latest", payload)
        ok2 = self._post(f"/readings/{self._device_id}", payload)
        return ok1 and ok2

    def _token(self) -> str:
        if time.monotonic() >= self._token_exp - 60:
            self._refresh_auth()
        return self._id_token

    def _refresh_auth(self):
        try:
            resp = self._requests.post(
                f"https://securetoken.googleapis.com/v1/token?key={self._api_key}",
                json={"grant_type": "refresh_token", "refresh_token": self._refresh_token},
                timeout=10,
            )
            body = resp.json()
            self._id_token      = body["id_token"]
            self._refresh_token = body["refresh_token"]
            self._token_exp     = time.monotonic() + int(body.get("expires_in", 3600))
        except Exception as e:
            print(f"[FB] Token refresh failed: {e}")

    def _url(self, path: str) -> str:
        return f"{self._db_url}{path}.json?auth={self._token()}"

    def _patch(self, path: str, data: dict) -> bool:
        try:
            r = self._requests.patch(self._url(path), json=data, timeout=5)
            return r.status_code == 200
        except Exception as e:
            print(f"[FB] PATCH {path}: {e}")
            return False

    def _put(self, path: str, data: dict) -> bool:
        try:
            r = self._requests.put(self._url(path), json=data, timeout=5)
            return r.status_code == 200
        except Exception as e:
            print(f"[FB] PUT {path}: {e}")
            return False

    def _post(self, path: str, data: dict) -> bool:
        try:
            r = self._requests.post(self._url(path), json=data, timeout=5)
            return r.status_code == 200
        except Exception as e:
            print(f"[FB] POST {path}: {e}")
            return False

    def _get(self, path: str):
        try:
            r = self._requests.get(self._url(path), timeout=5)
            return r.json() if r.status_code == 200 else None
        except Exception:
            return None
