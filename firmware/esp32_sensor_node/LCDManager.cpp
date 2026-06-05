#include "LCDManager.h"
#include <stdio.h>

LCDManager::LCDManager(uint8_t addr, uint8_t cols, uint8_t rows)
    : _lcd(addr, cols, rows), _cols(cols), _rows(rows),
      _screen(0), _lastSwitch(0) {}

void LCDManager::begin() {
    _lcd.init();
    _lcd.backlight();
    _lcd.clear();
    _print("Iris Biometric", "  Access Ctrl");
    Serial.println("[LCD] LCDManager ready");
}

void LCDManager::tickSensorScreens(const SensorReading& s) {
    if (millis() - _lastSwitch < SCREEN_MS) return;
    _lastSwitch = millis();

    char l1[17], l2[17];
    switch (_screen % 3) {
        case 0:  // Temperature + Humidity
            snprintf(l1, 17, "Temp: %5.1f C   ", s.temperatureC);
            snprintf(l2, 17, "Humi: %5.1f %%  ", s.humidityPct);
            break;
        case 1:  // Smoke
            snprintf(l1, 17, "Smoke:%5.1f %%  ", s.smokePct);
            snprintf(l2, 17, s.smokePct > 50.0f ? "  !! DANGER !!  " : "   Level: OK    ");
            break;
        case 2:  // Distance
            if (s.distanceCm < 0) {
                snprintf(l1, 17, "Dist: out range ");
            } else {
                snprintf(l1, 17, "Dist:%6.1f cm  ", s.distanceCm);
            }
            snprintf(l2, 17, "Press key: auth ");
            break;
    }
    _screen++;
    _print(l1, l2);
}

void LCDManager::showMessage(const char* line1, const char* line2) {
    _print(line1, line2);
}

void LCDManager::showPinEntry(const char* masked) {
    _print("Enter PIN:", masked);
}

void LCDManager::showAuth(bool success, const char* detail) {
    if (success) {
        _print("Access Granted!", detail);
    } else {
        _print("Access Denied!", detail);
    }
}

void LCDManager::_print(const char* line1, const char* line2) {
    _lcd.clear();
    _lcd.setCursor(0, 0);
    _lcd.print(line1);
    _lcd.setCursor(0, 1);
    _lcd.print(line2);
}
