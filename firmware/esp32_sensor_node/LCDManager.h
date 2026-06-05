#pragma once
/*
 * LCDManager.h
 * Drives a 16×2 I2C LCD (PCF8574T backpack, address 0x27).
 *
 * tick() cycles through three sensor screens every 3 s in MONITORING.
 * One-shot methods (showMessage, showAuth, showPinEntry) take over
 * the display immediately and stay until tick() is re-enabled or the
 * next showX call.
 *
 * Required library: LiquidCrystal I2C by Frank de Brabander
 *   Arduino IDE → Library Manager → "LiquidCrystal I2C"
 */

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include "SensorManager.h"

class LCDManager {
public:
    LCDManager(uint8_t addr = 0x27, uint8_t cols = 16, uint8_t rows = 2);

    void begin();

    // Call each loop() to rotate through sensor screens in MONITORING.
    // Stop calling (or call showMessage/showAuth) to freeze the display.
    void tickSensorScreens(const SensorReading& s);

    // Generic two-line message; persists until next call
    void showMessage(const char* line1, const char* line2 = "");

    // "Enter PIN:" top, masked asterisks bottom  e.g. showPinEntry("***")
    void showPinEntry(const char* masked);

    // Auth result — stays until caller transitions state
    void showAuth(bool success, const char* detail = "");

private:
    LiquidCrystal_I2C _lcd;
    uint8_t  _cols, _rows;
    uint8_t  _screen;
    unsigned long _lastSwitch;

    static constexpr uint32_t SCREEN_MS = 3000;

    void _print(const char* line1, const char* line2);
};
