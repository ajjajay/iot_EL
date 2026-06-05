#pragma once
/*
 * KeypadManager.h
 * Wraps the Keypad library for the 3×4 matrix keypad.
 *
 * Pin mapping (fixed — matches physical wiring):
 *   ROW1=13  ROW2=12  ROW3=14  ROW4=27
 *   COL1=26  COL2=25  COL3=33
 *
 * Key layout:
 *   1 2 3
 *   4 5 6
 *   7 8 9
 *   * 0 #
 *
 * Required library: Keypad by Mark Stanley, Alexander Brevig
 *   Arduino IDE → Library Manager → "Keypad"
 */

#include <Arduino.h>
#include <Keypad.h>

class KeypadManager {
public:
    KeypadManager();
    void begin();

    // Returns the key character, or '\0' / NO_KEY if nothing pressed
    char getKey();

private:
    Keypad _kp;

    static char _keys[4][3];
    static byte _rowPins[4];
    static byte _colPins[3];
};
