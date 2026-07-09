#include "KeypadManager.h"

char KeypadManager::_keys[4][3] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
};

byte KeypadManager::_rowPins[4] = {13, 12, 14, 27};
byte KeypadManager::_colPins[3] = {26, 25, 33};

KeypadManager::KeypadManager()
    : _kp(makeKeymap(_keys), _rowPins, _colPins, 4, 3) {}

void KeypadManager::begin() {
    Serial.println("[KPD] Keypad ready (4x3 matrix)");
}

char KeypadManager::getKey() {
    char k = _kp.getKey();
    return (k == NO_KEY) ? '\0' : k;
}
