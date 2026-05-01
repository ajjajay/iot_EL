#include "StateManager.h"

const char* stateToString(DeviceState s) {
    switch (s) {
        case DeviceState::INIT:           return "INIT";
        case DeviceState::CONNECTING:     return "CONNECTING";
        case DeviceState::READY:          return "READY";
        case DeviceState::MONITORING:     return "MONITORING";
        case DeviceState::ALERT:          return "ALERT";
        case DeviceState::ERROR:          return "ERROR";
        case DeviceState::ENROLLING:      return "ENROLLING";
        case DeviceState::AUTHENTICATING: return "AUTHENTICATING";
        case DeviceState::AUTHENTICATED:  return "AUTHENTICATED";
        case DeviceState::REJECTED:       return "REJECTED";
        default:                          return "UNKNOWN";
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
            return to == DeviceState::MONITORING    ||  // self (no-op cycle)
                   to == DeviceState::ALERT         ||  // anomaly detected
                   to == DeviceState::CONNECTING    ||  // WiFi lost
                   to == DeviceState::ENROLLING     ||  // enrollment triggered
                   to == DeviceState::AUTHENTICATING;   // auth triggered

        case DeviceState::ENROLLING:
            return to == DeviceState::MONITORING;       // done or timed out

        case DeviceState::AUTHENTICATING:
            return to == DeviceState::AUTHENTICATED ||
                   to == DeviceState::REJECTED;

        case DeviceState::AUTHENTICATED:
            return to == DeviceState::ALERT       ||   // anomaly follow-up
                   to == DeviceState::MONITORING;      // display timeout

        case DeviceState::REJECTED:
            return to == DeviceState::MONITORING;

        case DeviceState::ALERT:
            return to == DeviceState::MONITORING;

        case DeviceState::ERROR:
            return to == DeviceState::INIT;

        default:
            return false;
    }
}

void StateManager::_logTransition(DeviceState from, DeviceState to) const {
    Serial.printf("[FSM] %s → %s  (after %lums, transition #%u)\n",
                  stateToString(from), stateToString(to),
                  timeInState(), _transitionCount + 1);
}
