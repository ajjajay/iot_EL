#!/usr/bin/env python3
"""
Iris Biometric Access Control — Raspberry Pi edge node.

Mirrors esp32_sensor_node.ino state-machine and data flow exactly:
  INIT → CONNECTING → READY → MONITORING
  MONITORING → AUTHENTICATING → AUTHENTICATED / REJECTED → MONITORING
  MONITORING → ENROLLING → MONITORING
  MONITORING / AUTHENTICATED → ALERT → MONITORING
  any state → ERROR (restarts process)

Run:  python3 main.py
Mock: SENSOR_MOCK=1 python3 main.py
"""

import os
import sys
import signal
import time

from config_manager   import ConfigManager
from state_manager    import StateManager, DeviceState
from sensor_manager   import SensorManager
from actuator_controller import ActuatorController, RelayState, LedPattern
from iris_camera      import IrisCamera
from biometric_manager import BiometricManager
from anomaly_detector  import AnomalyDetector, BRUTE_FORCE_LIMIT
from alert_manager     import AlertManager
from ml_inference      import MLInference
from firebase_manager  import FirebaseManager
from aws_iot_manager   import AWSIoTManager


def _ms() -> float:
    return time.monotonic() * 1000


def main():
    print("\n=== Iris Biometric Access Control — Boot (Raspberry Pi) ===")

    # ── Configuration ─────────────────────────────────────────────────────────
    config = ConfigManager()
    config.load()
    c = config.cfg()

    # ── Modules ───────────────────────────────────────────────────────────────
    fsm       = StateManager()
    sensors   = SensorManager(c.dht_pin, c.ldr_spi_channel)
    actuators = ActuatorController(c.relay_pin, c.led_pin)
    camera    = IrisCamera()
    biometric = BiometricManager()
    anomaly   = AnomalyDetector(c.iris_match_threshold)
    ml        = MLInference(c.tflite_model_path)

    sensors.begin()
    actuators.begin()
    actuators.set_led_pattern(LedPattern.BLINK_FAST)

    if not ml.begin():
        print("[BOOT] Env ML disabled — threshold-only mode")

    if not camera.begin():
        print("[BOOT] Camera init failed — HALTING")
        fsm.transition(DeviceState.ERROR)
        actuators.set_led_pattern(LedPattern.BLINK_SOS)
        while True:
            actuators.tick()
            time.sleep(0.01)

    biometric.begin()

    # ── Firebase ──────────────────────────────────────────────────────────────
    firebase = None
    if c.firebase_url and c.firebase_api_key:
        firebase = FirebaseManager(c.firebase_api_key, c.firebase_url,
                                   c.firebase_email, c.firebase_password,
                                   c.device_id)
        if not firebase.begin():
            print("[BOOT] Firebase unavailable — offline mode")
            firebase = None
    else:
        print("[BOOT] Firebase not configured")

    # ── AWS IoT ───────────────────────────────────────────────────────────────
    aws_iot = None
    if c.aws_enabled and c.aws_endpoint:
        aws_iot = AWSIoTManager(c.aws_endpoint, c.aws_thing_name,
                                c.aws_ca_path, c.aws_cert_path, c.aws_key_path)
        if not aws_iot.begin():
            print("[BOOT] AWS IoT connect failed — will retry each heartbeat")

    alert_mgr = AlertManager(c.aws_thing_name or c.device_id, aws_iot, firebase)

    # ── GPIO button ───────────────────────────────────────────────────────────
    gpio = None
    try:
        import RPi.GPIO as GPIO
        GPIO.setmode(GPIO.BCM)
        GPIO.setup(c.auth_button_pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)
        gpio = GPIO
    except ImportError:
        print("[BOOT] RPi.GPIO unavailable — physical button disabled")

    # ── FSM boot sequence ─────────────────────────────────────────────────────
    fsm.transition(DeviceState.CONNECTING)
    # Network is managed by the OS on RPi; just confirm Firebase connected
    fsm.transition(DeviceState.READY)
    fsm.transition(DeviceState.MONITORING)
    actuators.set_led_pattern(LedPattern.BLINK_SLOW)
    print(f"[BOOT] Device '{c.device_id}' ready — {biometric.user_count()} user(s) enrolled")

    # ── Timing state ──────────────────────────────────────────────────────────
    last_heartbeat_ms  = 0.0
    last_command_ms    = 0.0
    last_env_sensor_ms = 0.0

    button_pressed_at  = 0.0
    button_was_pressed = False

    enroll_pending   = False
    pending_user_id  = ""
    pending_name     = ""

    # ── Graceful shutdown ─────────────────────────────────────────────────────
    def shutdown(sig, frame):
        print("\n[MAIN] Shutting down")
        if firebase:
            firebase.set_online(False)
        camera.close()
        actuators.cleanup()
        sys.exit(0)

    signal.signal(signal.SIGINT,  shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    # ── Button helpers ────────────────────────────────────────────────────────
    def is_short_press() -> bool:
        nonlocal button_was_pressed, button_pressed_at
        if gpio is None:
            return False
        down = gpio.input(c.auth_button_pin) == gpio.LOW
        now  = _ms()
        if down and not button_was_pressed:
            button_was_pressed = True
            button_pressed_at  = now
            return False
        if not down and button_was_pressed:
            button_was_pressed = False
            held = now - button_pressed_at
            if c.button_debounce_ms <= held < 3000:
                return True
        return False

    def is_long_press() -> bool:
        if not button_was_pressed:
            return False
        return (_ms() - button_pressed_at) >= c.button_long_press_ms

    # ── State handlers ────────────────────────────────────────────────────────
    def handle_enrolling():
        nonlocal enroll_pending, pending_user_id, pending_name
        print(f"[ENROLL] Starting for '{pending_user_id}' ('{pending_name}')")
        actuators.set_led_pattern(LedPattern.BLINK_FAST)

        iris = camera.capture_average(c.iris_enroll_frames, 200)
        if not iris.valid:
            print("[ENROLL] Capture failed — aborting")
            actuators.set_led_pattern(LedPattern.BLINK_SLOW)
            fsm.transition(DeviceState.MONITORING)
            enroll_pending = False
            return

        if biometric.enroll(pending_user_id, pending_name, iris):
            print(f"[ENROLL] Success for '{pending_user_id}'")
            if firebase:
                firebase.push_enrollment(pending_user_id, pending_name)
            actuators.set_led_pattern(LedPattern.ON)
            time.sleep(2.0)
        else:
            print("[ENROLL] Save failed")

        actuators.set_led_pattern(LedPattern.BLINK_SLOW)
        enroll_pending  = False
        pending_user_id = ""
        pending_name    = ""
        fsm.transition(DeviceState.MONITORING)

    def handle_authenticating():
        actuators.set_led_pattern(LedPattern.BLINK_FAST)

        iris = camera.capture()
        if not iris.valid:
            print("[AUTH] Camera capture failed")
            fsm.transition(DeviceState.REJECTED)
            return

        result     = biometric.match(iris, c.iris_match_threshold)
        env        = sensors.read_now()

        if result.matched:
            fsm.transition(DeviceState.AUTHENTICATED)
            actuators.set_relay(RelayState.ON)
            actuators.set_led_pattern(LedPattern.ON)

            anom_score = anomaly.record(result, True)
            if firebase:
                firebase.push_sign_in(result.user_id, result.user_name,
                                      result.score, True, anom_score)
            if aws_iot and aws_iot.is_connected():
                aws_iot.publish_biometric_event(result.user_id, result.score, True)

            if anom_score >= c.anomaly_score_threshold:
                print(f"[AUTH] Anomaly (score={anom_score:.3f}) — alerting")
                atype = ("brute_force"
                         if anomaly.consecutive_failures() >= BRUTE_FORCE_LIMIT
                         else "suspicious_signin")
                alert_mgr.send_anomaly(result.user_id, anom_score, atype)

        else:
            fsm.transition(DeviceState.REJECTED)
            actuators.set_led_pattern(LedPattern.BLINK_FAST)

            anom_score = anomaly.record(result, False)
            uid  = result.user_id   or "unknown"
            name = result.user_name or "Unknown"
            if firebase:
                firebase.push_sign_in(uid, name, result.score, False, anom_score)
            if aws_iot and aws_iot.is_connected():
                aws_iot.publish_biometric_event(uid, result.score, False)

            bf = anomaly.brute_force_score()
            if bf >= c.anomaly_score_threshold:
                alert_mgr.send_anomaly("unknown", bf, "brute_force",
                                       "Repeated failed iris attempts")

        # Ambient context alongside every sign-in attempt
        if env.valid and aws_iot and aws_iot.is_connected():
            ml_result = ml.infer(env.temperature_c, env.humidity_pct, env.light_norm)
            aws_iot.publish_reading(env, ml_result, fsm.current())

    # ── Main loop ─────────────────────────────────────────────────────────────
    while True:
        now   = _ms()
        state = fsm.current()
        actuators.tick()

        # Agent ACK from Bedrock via AWS IoT subscription
        if aws_iot:
            ack = aws_iot.get_agent_ack()
            if ack:
                alert_mgr.on_agent_ack(*ack)

        # Heartbeat
        if now - last_heartbeat_ms >= c.heartbeat_interval_ms:
            last_heartbeat_ms = now
            if firebase:
                firebase.send_heartbeat(fsm.current())
            if aws_iot and aws_iot.is_connected():
                aws_iot.publish_heartbeat(fsm.current())

        # Poll Firebase commands every 5 s
        if now - last_command_ms >= 5_000:
            last_command_ms = now
            if firebase:
                relay_cmd = firebase.poll_commands()
                if relay_cmd is not None:
                    actuators.set_relay_override(relay_cmd, 300_000)

                if not enroll_pending:
                    enroll_cmd = firebase.poll_enroll_command()
                    if enroll_cmd:
                        pending_user_id, pending_name = enroll_cmd
                        enroll_pending = True

        # AWS relay command via MQTT
        if aws_iot:
            aws_relay = aws_iot.get_relay_command()
            if aws_relay is not None:
                actuators.set_relay_override(aws_relay, 300_000)

        state = fsm.current()

        # ── MONITORING ────────────────────────────────────────────────────────
        if state == DeviceState.MONITORING:
            if enroll_pending:
                fsm.transition(DeviceState.ENROLLING)

            elif is_long_press() and not enroll_pending:
                print("[BTN] Long press — waiting for Firebase enrollment command")
                actuators.set_led_pattern(LedPattern.BLINK_FAST)
                time.sleep(0.5)
                actuators.set_led_pattern(LedPattern.BLINK_SLOW)
                button_was_pressed = False

            elif is_short_press():
                print("[BTN] Short press — starting authentication")
                fsm.transition(DeviceState.AUTHENTICATING)

            # Background ambient sensor read
            if now - last_env_sensor_ms >= c.sensor_interval_ms:
                last_env_sensor_ms = now
                env = sensors.read_now()
                if env.valid:
                    ml_result = ml.infer(env.temperature_c, env.humidity_pct, env.light_norm)
                    if firebase:
                        firebase.push_reading(env, ml_result, fsm.current())
                    if aws_iot and aws_iot.is_connected():
                        aws_iot.publish_reading(env, ml_result, fsm.current())
                    if ml_result.valid and ml_result.risk_score >= c.ml_risk_threshold:
                        print(f"[ENV] High risk score {ml_result.risk_score:.3f} — entering ALERT")
                        fsm.transition(DeviceState.ALERT)
                        actuators.set_led_pattern(LedPattern.BLINK_FAST)

        # ── ENROLLING ─────────────────────────────────────────────────────────
        elif state == DeviceState.ENROLLING:
            handle_enrolling()

        # ── AUTHENTICATING ────────────────────────────────────────────────────
        elif state == DeviceState.AUTHENTICATING:
            handle_authenticating()

        # ── AUTHENTICATED / REJECTED: hold display then re-lock ───────────────
        elif state in (DeviceState.AUTHENTICATED, DeviceState.REJECTED):
            if fsm.time_in_state_ms() >= c.auth_display_ms:
                actuators.set_relay(RelayState.OFF)
                actuators.set_led_pattern(LedPattern.BLINK_SLOW)
                fsm.transition(DeviceState.MONITORING)

        # ── ALERT: 10 s visual alert then return to MONITORING ────────────────
        elif state == DeviceState.ALERT:
            if fsm.time_in_state_ms() >= 10_000:
                actuators.set_led_pattern(LedPattern.BLINK_SLOW)
                fsm.transition(DeviceState.MONITORING)

        # ── ERROR: restart process after 10 s ─────────────────────────────────
        elif state == DeviceState.ERROR:
            if fsm.time_in_state_ms() > 10_000:
                print("[MAIN] Restarting after ERROR timeout")
                os.execv(sys.executable, [sys.executable] + sys.argv)

        time.sleep(0.01)


if __name__ == "__main__":
    main()
