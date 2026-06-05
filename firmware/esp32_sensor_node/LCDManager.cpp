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

void LCDManager::tickSensorScreens(const SensorReading& s,
                                    float riskScore, uint8_t mlLabel,
                                    const char* state) {
    if (millis() - _lastSwitch < SCREEN_MS) return;
    _lastSwitch = millis();

    char l1[17], l2[17];
    switch (_screen % 4) {
        case 0:  // Temperature + Humidity
            snprintf(l1, 17, "T:%4.1fC  H:%4.1f%%", s.temperatureC, s.humidityPct);
            snprintf(l2, 17, "%-16s", state);
            break;
        case 1:  // Smoke + distance
            snprintf(l1, 17, "Smoke: %5.1f %%  ", s.smokePct);
            if (s.smokePct > 50.0f)
                snprintf(l2, 17, "  !! DANGER !!  ");
            else if (s.distanceCm < 0)
                snprintf(l2, 17, "Dist: no object ");
            else
                snprintf(l2, 17, "Dist: %5.1f cm  ", s.distanceCm);
            break;
        case 2: {  // Risk score + ML label
            static const char* lblStr[] = {"NORMAL", "WARNING", "CRITIC"};
            const char* lbl = (mlLabel < 3) ? lblStr[mlLabel] : "------";
            snprintf(l1, 17, "Risk: %3.0f%%  %-6s", riskScore * 100.0f, lbl);
            snprintf(l2, 17, "Press key: auth ");
            break;
        }
        case 3:  // Uptime
            snprintf(l1, 17, "Up: %7lus     ", millis() / 1000UL);
            snprintf(l2, 17, "%-16s", state);
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
