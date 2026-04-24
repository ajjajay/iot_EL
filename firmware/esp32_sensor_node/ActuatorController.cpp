#include "ActuatorController.h"

ActuatorController::ActuatorController(uint8_t relayPin, uint8_t ledPin,
                                        bool relayActiveLow)
    : _relayPin(relayPin), _ledPin(ledPin), _activeLow(relayActiveLow),
      _relayState(RelayState::OFF),
      _relayOverrideActive(false),
      _relayOverrideState(RelayState::OFF),
      _overrideExpiresAt(0),
      _ledPattern(LedPattern::OFF),
      _ledPhysical(false),
      _ledLastToggle(0),
      _sosStep(0) {}

void ActuatorController::begin() {
    pinMode(_relayPin, OUTPUT);
    pinMode(_ledPin,   OUTPUT);
    _writeRelay(RelayState::OFF);
    digitalWrite(_ledPin, LOW);
    Serial.println("[ACT] ActuatorController ready");
}

void ActuatorController::setRelay(RelayState state) {
    // Respect active manual override
    if (_relayOverrideActive) {
        if (millis() < _overrideExpiresAt || _overrideExpiresAt == 0) {
            Serial.println("[ACT] Relay command ignored (manual override active)");
            return;
        }
        // Override expired
        _relayOverrideActive = false;
        Serial.println("[ACT] Manual override expired");
    }
    _writeRelay(state);
}

void ActuatorController::setRelayOverride(RelayState state, uint32_t timeoutMs) {
    _relayOverrideActive = true;
    _relayOverrideState  = state;
    _overrideExpiresAt   = (timeoutMs > 0) ? millis() + timeoutMs : 0;
    _writeRelay(state);
    Serial.printf("[ACT] Manual relay override: %s (timeout: %ums)\n",
                  state == RelayState::ON ? "ON" : "OFF", timeoutMs);
}

void ActuatorController::clearRelayOverride() {
    _relayOverrideActive = false;
    Serial.println("[ACT] Manual override cleared");
}

void ActuatorController::setLedPattern(LedPattern p) {
    if (_ledPattern == p) return;
    _ledPattern    = p;
    _ledLastToggle = millis();
    _sosStep       = 0;

    if (p == LedPattern::OFF)       { _ledPhysical = false; digitalWrite(_ledPin, LOW); }
    else if (p == LedPattern::ON)   { _ledPhysical = true;  digitalWrite(_ledPin, HIGH); }
}

void ActuatorController::tick() {
    // Check override expiry on every tick
    if (_relayOverrideActive && _overrideExpiresAt > 0 &&
        millis() > _overrideExpiresAt) {
        clearRelayOverride();
        _writeRelay(RelayState::OFF);  // safe default on expiry
    }
    _tickLed();
}

// ── Private ───────────────────────────────────────────────────────────────────

void ActuatorController::_writeRelay(RelayState s) {
    _relayState = s;
    // Most relay modules are active-LOW: HIGH = off, LOW = on
    bool gpioLevel = (s == RelayState::ON) ? !_activeLow : _activeLow;
    digitalWrite(_relayPin, gpioLevel ? HIGH : LOW);
    Serial.printf("[ACT] Relay → %s\n", s == RelayState::ON ? "ON" : "OFF");
}

void ActuatorController::_tickLed() {
    unsigned long now = millis();

    switch (_ledPattern) {
        case LedPattern::OFF:
        case LedPattern::ON:
            return;  // static state, nothing to blink

        case LedPattern::BLINK_SLOW:  // 1 Hz — toggle every 500 ms
            if (now - _ledLastToggle >= 500) {
                _ledPhysical   = !_ledPhysical;
                digitalWrite(_ledPin, _ledPhysical);
                _ledLastToggle = now;
            }
            break;

        case LedPattern::BLINK_FAST:  // 5 Hz — toggle every 100 ms
            if (now - _ledLastToggle >= 100) {
                _ledPhysical   = !_ledPhysical;
                digitalWrite(_ledPin, _ledPhysical);
                _ledLastToggle = now;
            }
            break;

        case LedPattern::BLINK_SOS: {
            // S = 3 short (200 ms), O = 3 long (600 ms), S = 3 short
            static const uint16_t pattern[] = {
                200, 200, 200, 200, 200, 200,   // S (3 short)
                600, 200, 600, 200, 600, 200,   // O (3 long)
                200, 200, 200, 200, 200, 1000   // S + gap
            };
            static const uint8_t STEPS = sizeof(pattern) / sizeof(pattern[0]);
            if (now - _ledLastToggle >= pattern[_sosStep % STEPS]) {
                _ledPhysical   = !_ledPhysical;
                digitalWrite(_ledPin, _ledPhysical);
                _ledLastToggle = now;
                _sosStep       = (_sosStep + 1) % STEPS;
            }
            break;
        }
    }
}
