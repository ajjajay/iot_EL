import json
import time

_ALERT_COOLDOWN_MS = 30_000.0


class AlertManager:
    """
    Alert dispatcher — mirrors AlertManager.h/.cpp.
    Suppresses duplicate alert types within the cooldown window.
    Publishes to AWS IoT (→ Lambda → Bedrock Agent) and logs to Firebase.
    """

    def __init__(self, thing_name: str, aws_iot, firebase):
        self._thing_name      = thing_name
        self._aws             = aws_iot
        self._fb              = firebase
        self._last_alert_ms   = 0.0
        self._last_alert_type = ""

    def send_anomaly(self, user_id: str, anomaly_score: float,
                     alert_type: str, detail: str = "") -> bool:
        now_ms = time.time() * 1000

        if (self._last_alert_type == alert_type and
                (now_ms - self._last_alert_ms) < _ALERT_COOLDOWN_MS):
            print(f"[ALERT] Cooldown active for '{alert_type}' — suppressed")
            return False

        payload = json.dumps({
            "deviceId":    self._thing_name,
            "userId":      user_id,
            "alertType":   alert_type,
            "anomalyScore": anomaly_score,
            "detail":      detail,
            "ts":          int(now_ms),
        })

        dispatched = False
        if self._aws:
            dispatched = self._aws.publish_alert_json(payload)
        if self._fb:
            self._fb.push_biometric_alert(user_id, alert_type, anomaly_score)

        if dispatched or self._fb:
            self._last_alert_ms   = now_ms
            self._last_alert_type = alert_type
            print(f"[ALERT] Sent: type={alert_type} user={user_id} score={anomaly_score:.3f}")

        return dispatched

    def on_agent_ack(self, user_id: str, alert_type: str):
        print(f"[ALERT] Agent ACK: type={alert_type} user={user_id} — notification delivered")
        # Extend cooldown so the same alert isn't re-sent immediately after ACK
        self._last_alert_ms = time.time() * 1000
