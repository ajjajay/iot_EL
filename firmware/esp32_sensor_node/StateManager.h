#pragma once
/*
 * StateManager.h
 * Finite State Machine (FSM) for the ESP32 sensor node.
 *
 * States:
 *   INIT        – Boot, hardware init
 *   CONNECTING  – WiFi + Firebase handshake
 *   READY       – Connected, waiting for first sensor read
 *   MONITORING  – Steady-state: read → infer → push → sleep
 *   ALERT       – Risk score above threshold; actuators engaged
 *   ERROR       – Unrecoverable fault; restart pending
 *
 * Transitions are intentionally one-directional to prevent race conditions.
 * Call tick() once per loop(); all side-effects live in onEnter/onExit hooks.
 */

#include <Arduino.h>

enum class DeviceState : uint8_t {
    INIT             = 0,
    CONNECTING       = 1,
    READY            = 2,
    MONITORING       = 3,   // idle — waiting for auth trigger
    ALERT            = 4,   // anomaly detected, alert dispatched
    ERROR            = 5,
    ENROLLING        = 6,   // capturing iris for new user template
    AUTHENTICATING   = 7,   // iris captured, running match
    AUTHENTICATED    = 8,   // access granted (relay energised)
    REJECTED         = 9    // no match — access denied
};

// Human-readable labels (useful for Serial and Firebase logging)
const char* stateToString(DeviceState s);

class StateManager {
public:
    StateManager();

    // Returns current state
    DeviceState current() const { return _state; }

    // Request a transition; returns true if the transition is legal
    bool transition(DeviceState next);

    // Returns how long (ms) we've been in the current state
    unsigned long timeInState() const;

    // Returns the total number of transitions since boot
    uint32_t transitionCount() const { return _transitionCount; }

private:
    DeviceState   _state;
    DeviceState   _previous;
    unsigned long _enteredAt;
    uint32_t      _transitionCount;

    // True if moving from `from` to `to` is a legal transition
    bool _isLegal(DeviceState from, DeviceState to) const;

    void _logTransition(DeviceState from, DeviceState to) const;
};
