#pragma once
/*
 * ActuatorController.h
 * Relay and LED are not present in the current hardware build.
 * This file is kept as a stub so that RelayState / LedPattern enums
 * remain available to FirebaseManager and AWSIoTManager without changes.
 * All methods are no-ops.
 */

#include <Arduino.h>

enum class RelayState : uint8_t { OFF = 0, ON = 1 };
enum class LedPattern  : uint8_t { OFF = 0, ON = 1, BLINK_SLOW = 2, BLINK_FAST = 3, BLINK_SOS = 4 };

class ActuatorController {
public:
    ActuatorController(uint8_t relayPin = 0, uint8_t ledPin = 0, bool activeLow = true) {}
    void begin() {}
    void setRelay(RelayState) {}
    void setRelayOverride(RelayState, uint32_t = 0) {}
    void clearRelayOverride() {}
    void setLedPattern(LedPattern) {}
    void tick() {}
    RelayState  relayState()   const { return RelayState::OFF; }
    LedPattern  ledPattern()   const { return LedPattern::OFF; }
    bool        hasRelayOverride() const { return false; }
};
