/*
 * keypad_test.ino
 * ─────────────────────────────────────────────────────────────────────────────
 * Standalone keypad test — prints every key press to Serial.
 * No WiFi / Firebase / sensors needed.
 *
 * Wiring (matches main firmware):
 *   ROW1=13  ROW2=12  ROW3=14  ROW4=27
 *   COL1=26  COL2=25  COL3=33
 *
 * Required library: Keypad by Mark Stanley, Alexander Brevig
 *   Arduino IDE → Library Manager → search "Keypad"
 *
 * Open Serial Monitor at 115200 baud, then press keys.
 */

#include <Keypad.h>

// ── Layout ────────────────────────────────────────────────────────────────────
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
};

byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33};

Keypad kp = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n========================================");
    Serial.println("  KEYPAD TEST — 115200 baud");
    Serial.println("  ROW: 13 12 14 27   COL: 26 25 33");
    Serial.println("  Press any key...");
    Serial.println("========================================");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    char key = kp.getKey();
    if (key) {
        Serial.printf("[KEY] '%c' pressed   (millis=%lu)\n", key, millis());
    }
}
