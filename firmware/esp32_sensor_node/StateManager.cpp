#include "StateManager.h"

const char* stateToString(DeviceState s) {
    switch (s) {
        case DeviceState::INIT:       return "INIT";
        case DeviceState::CONNECTING: return "CONNECTING";
        case DeviceState::READY:      return "READY";
        case DeviceState::MONITORING: return "MONITORING";
        case DeviceState::ALERT:      return "ALERT";
        case DeviceState::ERROR:      return "ERROR";
        default:                      return "UNKNOWN";
    }
}

StateManager::StateManager()
    : _state(DeviceState::INIT),
      _previous(DeviceState::INIT),
      _enteredAt(millis()),
      _transitionCount(0) {}

bool StateManager::transition(DeviceState next) {
    if (!_isLegal(_state, next)) {
        Serial.printf("[FSM] ILLEGAL transition %s → %s\n",
                      stateToString(_state), stateToString(next));
        return false;
    }
    _logTransition(_state, next);
    _previous        = _state;
    _state           = next;
    _enteredAt       = millis();
    _transitionCount++;
    return true;
}

unsigned long StateManager::timeInState() const {
    return millis() - _enteredAt;
}

// ── Legal transition table ────────────────────────────────────────────────────
// Encoding: from any state you can always go to ERROR (fault recovery).
// Otherwise only the listed paths are valid.
bool StateManager::_isLegal(DeviceState from, DeviceState to) const {
    if (to == DeviceState::ERROR) return true;   // fault can happen anywhere

    switch (from) {
        case DeviceState::INIT:
            return to == DeviceState::CONNECTING;

        case DeviceState::CONNECTING:
            return to == DeviceState::READY;

        case DeviceState::READY:
            return to == DeviceState::MONITORING;

        case DeviceState::MONITORING:
            // Normal cycle + alert escalation
            return to == DeviceState::MONITORING ||
                   to == DeviceState::ALERT       ||
                   to == DeviceState::CONNECTING;   // WiFi lost → reconnect

        case DeviceState::ALERT:
            return to == DeviceState::MONITORING;   // alert cleared

        case DeviceState::ERROR:
            return to == DeviceState::INIT;         // after restart

        default:
            return false;
    }
}

void StateManager::_logTransition(DeviceState from, DeviceState to) const {
    Serial.printf("[FSM] %s → %s  (after %lums, transition #%u)\n",
                  stateToString(from), stateToString(to),
                  timeInState(), _transitionCount + 1);
}
