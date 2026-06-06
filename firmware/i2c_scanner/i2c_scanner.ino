/*
 * i2c_scanner.ino
 * Scans all 127 I2C addresses and prints which ones respond.
 * SDA = 21, SCL = 22 (ESP32 default)
 * Open Serial Monitor at 115200 baud.
 */

#include <Wire.h>

void setup() {
    Serial.begin(115200);
    delay(1000);
    Wire.begin(21, 22);

    Serial.println("\nI2C Scanner — scanning addresses 0x01 to 0x7F...\n");

    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 128; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            Serial.printf("  Device found at 0x%02X\n", addr);
            found++;
        }
    }

    if (found == 0)
        Serial.println("  No I2C devices found — check SDA/SCL wiring and power.");
    else
        Serial.printf("\n%u device(s) found.\n", found);

    Serial.println("\nUse the address above in sensor_test.ino → #define LCD_ADDR");
}

void loop() {}
