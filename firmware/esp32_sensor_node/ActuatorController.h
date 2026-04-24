#pragma once
/*
 * ActuatorController.h
 * Controls relay (fan/heater) and status LED.
 *
 * Design decisions:
 *   - Relay is active-LOW on most breakout boards; invert flag handles both.
 *   - LED supports both solid on/off and blink patterns for state indication.
 *   - Commands can come from two sources: FSM/ML inference or manual override
 *     (from Firebase dashboard). Manual overrides expire after a timeout to
 *     prevent permanently stuck actuators if connectivity is lost.
 */

#include <Arduino.h>

enum class RelayState : uint8_t { OFF = 0, ON = 1 };
enum class LedPattern  : uint8_t {
    OFF          = 0,
    ON           = 1,
    BLINK_SLOW   = 2,   // 1 Hz  — normal operation
    BLINK_FAST   = 3,   // 5 Hz  — alert / error
    BLINK_SOS    = 4    // SOS morse
};

class ActuatorController {
public:
    // activeLow: set true if your relay module triggers on LOW signal
    ActuatorController(uint8_t relayPin, uint8_t ledPin,
                       bool relayActiveLow = true);

    void begin();

    // ── Relay ─────────────────────────────────────────────────────────────────
    void setRelay(RelayState state);
    RelayState relayState() const { return _relayState; }

    // Manual override from dashboard; expires after timeoutMs (0 = no expiry)
    void setRelayOverride(RelayState state, uint32_t timeoutMs = 300000);
    bool hasRelayOverride() const { return _relayOverrideActive; }
    void clearRelayOverride();

    // ── LED ──────────────────────────────────────────────────────────────────
    void setLedPattern(LedPattern p);
    LedPattern ledPattern() const { return _ledPattern; }

    // Must be called in loop() to service LED blink logic
    void tick();

private:
    uint8_t    _relayPin;
    uint8_t    _ledPin;
    bool       _activeLow;

    RelayState _relayState;
    bool       _relayOverrideActive;
    RelayState _relayOverrideState;
    uint32_t   _overrideExpiresAt;

    LedPattern    _ledPattern;
    bool          _ledPhysical;       // actual GPIO state
    unsigned long _ledLastToggle;
    uint8_t       _sosStep;

    void _writeRelay(RelayState s);
    void _tickLed();
};
