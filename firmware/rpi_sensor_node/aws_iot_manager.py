import json
import ssl
import threading
import time
from typing import Optional, Tuple

from sensor_manager import SensorReading
from ml_inference import MLResult
from state_manager import DeviceState
from actuator_controller import RelayState

_CONNECT_TIMEOUT_S = 5.0


class AWSIoTManager:
    """
    MQTT/TLS bridge to AWS IoT Core via paho-mqtt.
    Mirrors AWSIoTManager.h/.cpp — same topics, same shadow schema.
    paho runs its own keepalive thread via loop_start(); no manual loop() call needed.
    """

    def __init__(self, endpoint: str, thing_name: str,
                 ca_path: str, cert_path: str, key_path: str):
        self._endpoint   = endpoint
        self._thing_name = thing_name
        self._ca_path    = ca_path
        self._cert_path  = cert_path
        self._key_path   = key_path
        self._client     = None
        self._lock       = threading.Lock()

        self._relay_pending    = False
        self._pending_relay: Optional[RelayState] = None
        self._ack_pending      = False
        self._ack_user_id      = ""
        self._ack_alert_type   = ""

    def begin(self) -> bool:
        try:
            import paho.mqtt.client as mqtt
        except ImportError:
            print("[AWS] paho-mqtt not installed — AWS IoT disabled")
            return False

        client = mqtt.Client(client_id=self._thing_name, protocol=mqtt.MQTTv311)
        try:
            client.tls_set(
                ca_certs   = self._ca_path,
                certfile   = self._cert_path,
                keyfile    = self._key_path,
                tls_version = ssl.PROTOCOL_TLS_CLIENT,
            )
        except Exception as e:
            print(f"[AWS] TLS setup failed: {e}")
            return False

        client.on_connect    = self._on_connect
        client.on_message    = self._on_message
        client.on_disconnect = self._on_disconnect

        try:
            client.connect(self._endpoint, port=8883, keepalive=60)
        except Exception as e:
            print(f"[AWS] connect() failed: {e}")
            return False

        client.loop_start()
        self._client = client

        deadline = time.monotonic() + _CONNECT_TIMEOUT_S
        while not self.is_connected() and time.monotonic() < deadline:
            time.sleep(0.1)

        if self.is_connected():
            print(f"[AWS] Connected to {self._endpoint}")
            return True
        print("[AWS] Connect timeout")
        return False

    def loop(self):
        pass   # paho's loop_start() handles MQTT keep-alive in a background thread

    def publish_reading(self, s: SensorReading, ml: MLResult, state: DeviceState):
        self._pub(f"iot/{self._thing_name}/telemetry", {
            "deviceId":     self._thing_name,
            "temperatureC": s.temperature_c,
            "humidityPct":  s.humidity_pct,
            "lightNorm":    s.light_norm,
            "riskScore":    ml.risk_score,
            "mlLabel":      ml.label,
            "state":        state.value,
            "ts":           int(time.time() * 1000),
        })
        self._publish_shadow(s, ml, state)

    def publish_heartbeat(self, state: DeviceState):
        self._pub(f"iot/{self._thing_name}/heartbeat", {
            "deviceId": self._thing_name,
            "state":    state.value,
            "ts":       int(time.time() * 1000),
        })

    def publish_biometric_event(self, user_id: str, match_score: float, success: bool):
        self._pub(f"iot/{self._thing_name}/biometric/signin", {
            "deviceId":  self._thing_name,
            "userId":    user_id,
            "matchScore": match_score,
            "success":   success,
            "ts":        int(time.time() * 1000),
        })

    def publish_alert_json(self, alert_json: str) -> bool:
        if not self.is_connected():
            return False
        try:
            result = self._client.publish(
                f"iot/{self._thing_name}/biometric/alert", alert_json, qos=1
            )
            return result.rc == 0
        except Exception as e:
            print(f"[AWS] Alert publish failed: {e}")
            return False

    def get_relay_command(self) -> Optional[RelayState]:
        with self._lock:
            if self._relay_pending:
                self._relay_pending = False
                return self._pending_relay
        return None

    def get_agent_ack(self) -> Optional[Tuple[str, str]]:
        with self._lock:
            if self._ack_pending:
                self._ack_pending = False
                return (self._ack_user_id, self._ack_alert_type)
        return None

    def is_connected(self) -> bool:
        return self._client is not None and self._client.is_connected()

    # ── Private ───────────────────────────────────────────────────────────────

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            client.subscribe(f"iot/{self._thing_name}/commands")
            client.subscribe(f"$aws/things/{self._thing_name}/shadow/update/delta")
            client.subscribe(f"iot/{self._thing_name}/ai/alerts")
        else:
            print(f"[AWS] on_connect rc={rc}")

    def _on_disconnect(self, client, userdata, rc):
        if rc != 0:
            print(f"[AWS] Unexpected disconnect rc={rc}")

    def _on_message(self, client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode())
        except Exception:
            return
        topic = msg.topic

        if topic == f"iot/{self._thing_name}/commands":
            relay_str = payload.get("relay", "")
            with self._lock:
                self._relay_pending = True
                self._pending_relay = RelayState.ON if relay_str == "ON" else RelayState.OFF

        elif "shadow/update/delta" in topic:
            relay_str = payload.get("state", {}).get("relay", "")
            if relay_str:
                with self._lock:
                    self._relay_pending = True
                    self._pending_relay = RelayState.ON if relay_str == "ON" else RelayState.OFF

        elif topic == f"iot/{self._thing_name}/ai/alerts":
            if payload.get("ack"):
                with self._lock:
                    self._ack_pending     = True
                    self._ack_user_id     = payload.get("userId",    "")
                    self._ack_alert_type  = payload.get("alertType", "")

    def _pub(self, topic: str, payload: dict):
        if not self.is_connected():
            return
        try:
            self._client.publish(topic, json.dumps(payload), qos=0)
        except Exception as e:
            print(f"[AWS] Publish {topic}: {e}")

    def _publish_shadow(self, s: SensorReading, ml: MLResult, state: DeviceState):
        self._pub(f"$aws/things/{self._thing_name}/shadow/update", {
            "state": {"reported": {
                "temperatureC": s.temperature_c,
                "humidityPct":  s.humidity_pct,
                "riskScore":    ml.risk_score,
                "state":        state.value,
            }}
        })
