import time
import threading
from enum import Enum

# SOS pattern: list of on/off durations in seconds (alternating, starts with ON)
_SOS_DURATIONS = [
    0.10, 0.10,  # S: . .
    0.10, 0.10,
    0.10, 0.30,
    0.30, 0.10,  # O: - - -
    0.30, 0.10,
    0.30, 0.30,
    0.10, 0.10,  # S: . .
    0.10, 0.10,
    0.10, 0.70,  # gap before repeat
]


class RelayState(Enum):
    OFF = 0
    ON  = 1


class LedPattern(Enum):
    OFF        = 0
    ON         = 1
    BLINK_SLOW = 2   # 1 Hz
    BLINK_FAST = 3   # 5 Hz
    BLINK_SOS  = 4


class ActuatorController:
    def __init__(self, relay_pin: int, led_pin: int,
                 relay_active_low: bool = True):
        self._relay_pin     = relay_pin
        self._led_pin       = led_pin
        self._active_low    = relay_active_low
        self._relay_state   = RelayState.OFF
        self._ov_active     = False
        self._ov_state      = RelayState.OFF
        self._ov_expires_at = 0.0
        self._led_pattern   = LedPattern.OFF
        self._led_physical  = False
        self._led_toggled   = 0.0
        self._sos_step      = 0
        self._gpio          = None
        self._lock          = threading.Lock()

    def begin(self):
        try:
            import RPi.GPIO as GPIO
            GPIO.setmode(GPIO.BCM)
            GPIO.setwarnings(False)
            GPIO.setup(self._relay_pin, GPIO.OUT)
            GPIO.setup(self._led_pin,   GPIO.OUT)
            self._gpio = GPIO
            self._write_relay(RelayState.OFF)
            GPIO.output(self._led_pin, GPIO.LOW)
            print(f"[ACT] Relay GPIO{self._relay_pin}  LED GPIO{self._led_pin}  (BCM)")
        except ImportError:
            print("[ACT] RPi.GPIO unavailable — actuator outputs suppressed")

    def set_relay(self, state: RelayState):
        with self._lock:
            if self._ov_active:
                return
            self._relay_state = state
            self._write_relay(state)

    def relay_state(self) -> RelayState:
        return self._relay_state

    def set_relay_override(self, state: RelayState, timeout_ms: float = 300_000):
        with self._lock:
            self._ov_active     = True
            self._ov_state      = state
            self._ov_expires_at = time.monotonic() + timeout_ms / 1000
            self._write_relay(state)

    def has_relay_override(self) -> bool:
        return self._ov_active

    def clear_relay_override(self):
        with self._lock:
            self._ov_active = False

    def set_led_pattern(self, pattern: LedPattern):
        self._led_pattern = pattern
        self._sos_step    = 0
        if pattern == LedPattern.OFF:
            self._set_led(False)
        elif pattern == LedPattern.ON:
            self._set_led(True)

    def led_pattern(self) -> LedPattern:
        return self._led_pattern

    def tick(self):
        now = time.monotonic()

        # Override expiry
        with self._lock:
            if self._ov_active and now >= self._ov_expires_at:
                print("[ACT] Relay override expired — reverting")
                self._ov_active = False
                self._write_relay(self._relay_state)

        # LED blink logic
        p = self._led_pattern
        if p == LedPattern.BLINK_SLOW:
            if now - self._led_toggled >= 0.5:
                self._led_toggled = now
                self._set_led(not self._led_physical)
        elif p == LedPattern.BLINK_FAST:
            if now - self._led_toggled >= 0.1:
                self._led_toggled = now
                self._set_led(not self._led_physical)
        elif p == LedPattern.BLINK_SOS:
            interval = _SOS_DURATIONS[self._sos_step % len(_SOS_DURATIONS)]
            if now - self._led_toggled >= interval:
                self._led_toggled = now
                self._set_led(not self._led_physical)
                self._sos_step = (self._sos_step + 1) % len(_SOS_DURATIONS)

    def cleanup(self):
        if self._gpio:
            self._write_relay(RelayState.OFF)
            self._set_led(False)
            self._gpio.cleanup()

    # ── Private ───────────────────────────────────────────────────────────────

    def _set_led(self, on: bool):
        self._led_physical = on
        if self._gpio:
            self._gpio.output(self._led_pin,
                              self._gpio.HIGH if on else self._gpio.LOW)

    def _write_relay(self, state: RelayState):
        if self._gpio is None:
            return
        if self._active_low:
            level = self._gpio.LOW  if state == RelayState.ON else self._gpio.HIGH
        else:
            level = self._gpio.HIGH if state == RelayState.ON else self._gpio.LOW
        self._gpio.output(self._relay_pin, level)
