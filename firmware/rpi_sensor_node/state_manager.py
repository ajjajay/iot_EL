from enum import Enum
import time


class DeviceState(Enum):
    INIT           = "INIT"
    CONNECTING     = "CONNECTING"
    READY          = "READY"
    MONITORING     = "MONITORING"
    ALERT          = "ALERT"
    ERROR          = "ERROR"
    ENROLLING      = "ENROLLING"
    AUTHENTICATING = "AUTHENTICATING"
    AUTHENTICATED  = "AUTHENTICATED"
    REJECTED       = "REJECTED"


_LEGAL = {
    DeviceState.INIT:           {DeviceState.CONNECTING, DeviceState.ERROR},
    DeviceState.CONNECTING:     {DeviceState.READY, DeviceState.MONITORING, DeviceState.ERROR},
    DeviceState.READY:          {DeviceState.MONITORING, DeviceState.ERROR},
    DeviceState.MONITORING:     {DeviceState.AUTHENTICATING, DeviceState.ENROLLING,
                                 DeviceState.ALERT, DeviceState.CONNECTING, DeviceState.ERROR},
    DeviceState.AUTHENTICATING: {DeviceState.AUTHENTICATED, DeviceState.REJECTED, DeviceState.ERROR},
    DeviceState.AUTHENTICATED:  {DeviceState.MONITORING, DeviceState.ALERT, DeviceState.ERROR},
    DeviceState.REJECTED:       {DeviceState.MONITORING, DeviceState.ALERT, DeviceState.ERROR},
    DeviceState.ENROLLING:      {DeviceState.MONITORING, DeviceState.ERROR},
    DeviceState.ALERT:          {DeviceState.MONITORING, DeviceState.ERROR},
    DeviceState.ERROR:          set(),
}


class StateManager:
    def __init__(self):
        self._state = DeviceState.INIT
        self._entered_at = time.monotonic()

    def transition(self, new_state: DeviceState) -> bool:
        if new_state not in _LEGAL.get(self._state, set()):
            print(f"[FSM] Illegal transition {self._state.value} → {new_state.value}")
            return False
        elapsed_ms = (time.monotonic() - self._entered_at) * 1000
        print(f"[FSM] {self._state.value} → {new_state.value}  ({elapsed_ms:.0f} ms in prev state)")
        self._state = new_state
        self._entered_at = time.monotonic()
        return True

    def current(self) -> DeviceState:
        return self._state

    def time_in_state_ms(self) -> float:
        return (time.monotonic() - self._entered_at) * 1000
