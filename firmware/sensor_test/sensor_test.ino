/*
 * sensor_test.ino
 * ─────────────────────────────────────────────────────────────────────────────
 * Standalone hardware test — reads every sensor and logs to Serial + LCD.
 * No Firebase / AWS / TFLite / WiFi — just the physical sensors.
 *
 * Hardware:
 *   DHT11        — pin 4
 *   MQ-2         — pin 32  (analog ADC)
 *   HC-SR04      — TRIG=5  ECHO=18
 *   16×2 I2C LCD — SDA=21  SCL=22  addr=0x27
 *
 * Required libraries (Arduino Library Manager):
 *   DHT sensor library  (Adafruit)
 *   Adafruit Unified Sensor
 *   LiquidCrystal I2C   (Frank de Brabander)
 *
 * Open Serial Monitor at 115200 baud.
 * LCD cycles: screen A → screen B every READ_INTERVAL_MS.
 */

#include <Wire.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// ── Pin definitions ───────────────────────────────────────────────────────────
#define DHT_PIN        4
#define DHT_TYPE       DHT11
#define SMOKE_PIN      32
#define TRIG_PIN       5
#define ECHO_PIN       18
#define LCD_ADDR       0x27
#define LCD_COLS       16
#define LCD_ROWS       2

// ── Timing ────────────────────────────────────────────────────────────────────
#define READ_INTERVAL_MS  2000   // read + print every 2 s
#define LCD_FLIP_MS       3000   // flip LCD screen every 3 s

// ── Objects ───────────────────────────────────────────────────────────────────
DHT              dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// ── State ─────────────────────────────────────────────────────────────────────
unsigned long lastReadMs  = 0;
unsigned long lastFlipMs  = 0;
uint8_t       lcdScreen   = 0;

// Latest readings (updated each read cycle, used by LCD flip)
float    g_tempC      = 0;
float    g_humPct     = 0;
uint16_t g_smokeRaw   = 0;
float    g_smokePct   = 0;
float    g_distCm     = -1;
bool     g_dhtOk      = false;

// ── Helpers ───────────────────────────────────────────────────────────────────
float readDistance() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long dur = pulseIn(ECHO_PIN, HIGH, 30000UL);
    if (dur == 0) return -1.0f;
    return dur * 0.01715f;  // cm
}

void lcdPrint(const char* l1, const char* l2) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(l1);
    lcd.setCursor(0, 1); lcd.print(l2);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    // LCD — ESP32 needs explicit I2C pin init before the LCD driver
    Wire.begin(21, 22);   // SDA=21, SCL=22
    lcd.init();
    lcd.backlight();
    lcdPrint("Sensor Test", "Booting...");

    // Sensors
    dht.begin();
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);
    analogReadResolution(12);

    Serial.println("\n========================================");
    Serial.println("  SENSOR TEST — 115200 baud");
    Serial.println("  DHT11 | MQ-2 | HC-SR04 | LCD");
    Serial.println("========================================");

    delay(1500);
    lcdPrint("All sensors", "ready!");
    delay(1000);
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    // ── Read all sensors every READ_INTERVAL_MS ───────────────────────────────
    if (now - lastReadMs >= READ_INTERVAL_MS) {
        lastReadMs = now;

        // DHT11
        float t = dht.readTemperature();
        float h = dht.readHumidity();
        g_dhtOk = !(isnan(t) || isnan(h));
        if (g_dhtOk) { g_tempC = t; g_humPct = h; }

        // MQ-2 smoke
        g_smokeRaw = analogRead(SMOKE_PIN);
        g_smokePct = g_smokeRaw / 4095.0f * 100.0f;

        // HC-SR04 distance
        g_distCm = readDistance();

        // ── Serial output ─────────────────────────────────────────────────────
        Serial.println("----------------------------------------");
        Serial.printf("[DHT11]  Temp     : %s\n",
                      g_dhtOk ? String(g_tempC, 2) + " °C" : "READ FAILED");
        Serial.printf("[DHT11]  Humidity : %s\n",
                      g_dhtOk ? String(g_humPct, 2) + " %" : "READ FAILED");
        Serial.printf("[MQ-2]   Smoke raw: %u  /  4095\n", g_smokeRaw);
        Serial.printf("[MQ-2]   Smoke %%  : %.1f %%  (%s)\n",
                      g_smokePct,
                      g_smokePct > 60 ? "DANGER" :
                      g_smokePct > 25 ? "WARNING" : "OK");
        Serial.printf("[HCSR04] Distance : %s\n",
                      g_distCm < 0 ? "no object in range" :
                      String(g_distCm, 1) + " cm");
        Serial.printf("[SYS]    Uptime   : %lu s   Heap: %u bytes\n",
                      now / 1000, (uint32_t)ESP.getFreeHeap());
        Serial.println("----------------------------------------");
    }

    // ── Flip LCD screen every LCD_FLIP_MS ────────────────────────────────────
    if (now - lastFlipMs >= LCD_FLIP_MS) {
        lastFlipMs = now;
        char l1[17], l2[17];

        switch (lcdScreen % 3) {
            case 0:  // Temperature + humidity
                if (g_dhtOk) {
                    snprintf(l1, 17, "Temp: %5.1f C   ", g_tempC);
                    snprintf(l2, 17, "Humi: %5.1f %%  ", g_humPct);
                } else {
                    snprintf(l1, 17, "DHT11 FAIL      ");
                    snprintf(l2, 17, "Check wiring!   ");
                }
                break;

            case 1:  // Smoke
                snprintf(l1, 17, "Smoke:%5.1f %%  ", g_smokePct);
                snprintf(l2, 17, "%s",
                         g_smokePct > 60 ? "  !! DANGER !!  " :
                         g_smokePct > 25 ? "  ! WARNING !   " :
                                           "   Level: OK    ");
                break;

            case 2:  // Distance
                if (g_distCm < 0) {
                    snprintf(l1, 17, "Dist: no object ");
                } else {
                    snprintf(l1, 17, "Dist: %5.1f cm  ", g_distCm);
                }
                snprintf(l2, 17, "Uptime: %6lus  ", now / 1000UL);
                break;
        }

        lcdPrint(l1, l2);
        lcdScreen++;
    }

    delay(10);
}
