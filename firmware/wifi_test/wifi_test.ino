#include <WiFi.h>

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== WiFi Hardware Test ===");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    Serial.println("Scanning for networks...");
    int n = WiFi.scanNetworks();

    if (n == 0) {
        Serial.println("No networks found — WiFi may be faulty or antenna missing");
    } else {
        Serial.printf("%d network(s) found:\n", n);
        for (int i = 0; i < n; i++) {
            Serial.printf("  %d: %-32s | RSSI: %3d dBm | %s\n",
                i + 1,
                WiFi.SSID(i).c_str(),
                WiFi.RSSI(i),
                WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Secured");
        }
    }

    Serial.println("\nNow trying to connect to 'Girish's A14'...");
    WiFi.begin("Girish's A14", "uv8burfhzvy2hi2");

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Connected! IP: %s  RSSI: %d dBm\n",
            WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.printf("Failed. Status code: %d\n", WiFi.status());
        Serial.println("Codes: 1=NO_SSID_AVAIL  4=CONNECT_FAILED  6=DISCONNECTED");
    }
}

void loop() {}
