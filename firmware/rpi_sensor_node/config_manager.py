import json
import os
from dataclasses import dataclass

_CONFIG_PATH = os.path.join(os.path.dirname(__file__), "data", "config.json")


@dataclass
class DeviceConfig:
    # Identity
    device_id: str = "rpi_node_01"
    location: str = "Entrance A"

    # Firebase
    firebase_api_key: str = ""
    firebase_url: str = ""
    firebase_email: str = ""
    firebase_password: str = ""

    # AWS IoT Core
    aws_endpoint: str = ""
    aws_thing_name: str = "rpi_node_01"
    aws_enabled: bool = True
    aws_ca_path: str = "data/root-ca.crt"
    aws_cert_path: str = "data/device.crt"
    aws_key_path: str = "data/device.key"

    # GPIO pins (BCM numbering)
    dht_pin: int = 4
    ldr_spi_channel: int = 0     # MCP3008 SPI channel 0
    relay_pin: int = 17
    led_pin: int = 27
    auth_button_pin: int = 22

    # Timing (ms)
    sensor_interval_ms: int = 10_000
    heartbeat_interval_ms: int = 30_000
    button_debounce_ms: int = 50
    button_long_press_ms: int = 2_000
    auth_display_ms: int = 3_000

    # Env thresholds
    temp_warning_c: float = 30.0
    temp_critical_c: float = 40.0
    humidity_warning_pct: float = 80.0
    ml_risk_threshold: float = 0.65

    # Biometric
    iris_match_threshold: float = 0.30
    iris_enroll_frames: int = 5
    anomaly_score_threshold: float = 0.60
    alert_cooldown_ms: int = 30_000

    # TFLite model
    tflite_model_path: str = "data/ambient_model.tflite"


class ConfigManager:
    def __init__(self):
        self._cfg = DeviceConfig()

    def load(self) -> bool:
        if not os.path.exists(_CONFIG_PATH):
            print(f"[CFG] {_CONFIG_PATH} not found — using defaults")
            return True
        try:
            with open(_CONFIG_PATH, "r") as f:
                data = json.load(f)
            self._apply(data)
            print(f"[CFG] Loaded {_CONFIG_PATH}")
            return True
        except Exception as e:
            print(f"[CFG] Parse error: {e} — using defaults")
            return False

    def cfg(self) -> DeviceConfig:
        return self._cfg

    def _apply(self, d: dict):
        c = self._cfg
        c.device_id               = d.get("deviceId",              c.device_id)
        c.location                = d.get("location",              c.location)
        c.firebase_api_key        = d.get("firebaseApiKey",        c.firebase_api_key)
        c.firebase_url            = d.get("firebaseUrl",           c.firebase_url)
        c.firebase_email          = d.get("firebaseEmail",         c.firebase_email)
        c.firebase_password       = d.get("firebasePass",          c.firebase_password)
        c.aws_endpoint            = d.get("awsEndpoint",           c.aws_endpoint)
        c.aws_thing_name          = d.get("awsThingName",          c.aws_thing_name)
        c.aws_enabled             = d.get("awsEnabled",            c.aws_enabled)
        c.aws_ca_path             = d.get("awsCaPath",             c.aws_ca_path)
        c.aws_cert_path           = d.get("awsCertPath",           c.aws_cert_path)
        c.aws_key_path            = d.get("awsKeyPath",            c.aws_key_path)
        c.dht_pin                 = d.get("dhtPin",                c.dht_pin)
        c.ldr_spi_channel         = d.get("ldrSpiChannel",         c.ldr_spi_channel)
        c.relay_pin               = d.get("relayPin",              c.relay_pin)
        c.led_pin                 = d.get("ledPin",                c.led_pin)
        c.auth_button_pin         = d.get("authButtonPin",         c.auth_button_pin)
        c.sensor_interval_ms      = d.get("sensorIntervalMs",      c.sensor_interval_ms)
        c.heartbeat_interval_ms   = d.get("heartbeatIntervalMs",   c.heartbeat_interval_ms)
        c.button_debounce_ms      = d.get("buttonDebounceMs",      c.button_debounce_ms)
        c.button_long_press_ms    = d.get("buttonLongPressMs",     c.button_long_press_ms)
        c.auth_display_ms         = d.get("authDisplayMs",         c.auth_display_ms)
        c.temp_warning_c          = d.get("tempWarningC",          c.temp_warning_c)
        c.temp_critical_c         = d.get("tempCriticalC",         c.temp_critical_c)
        c.humidity_warning_pct    = d.get("humidityWarningPct",    c.humidity_warning_pct)
        c.ml_risk_threshold       = d.get("mlRiskThreshold",       c.ml_risk_threshold)
        c.iris_match_threshold    = d.get("irisMatchThreshold",    c.iris_match_threshold)
        c.iris_enroll_frames      = d.get("irisEnrollFrames",      c.iris_enroll_frames)
        c.anomaly_score_threshold = d.get("anomalyScoreThreshold", c.anomaly_score_threshold)
        c.alert_cooldown_ms       = d.get("alertCooldownMs",       c.alert_cooldown_ms)
        c.tflite_model_path       = d.get("tfliteModelPath",       c.tflite_model_path)
