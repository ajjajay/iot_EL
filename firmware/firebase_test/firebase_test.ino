#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <time.h>

// ── Credentials ───────────────────────────────────────────────────────────────
#define WIFI_SSID     "Girish's A14"
#define WIFI_PASS     "uv8burfhzvy2hi2"

#define FB_API_KEY    "AIzaSyBsbx7C15g-Ws6yIYtNo3zd7vU5geQwS8g"
#define FB_URL        "https://iot-fc8b3-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FB_EMAIL      "ajaygirish23@gmail.com"
#define FB_PASS       "Thragg"
#define DEVICE_ID     "esp32_node_01"

// ── Hardcoded test values (all 21) ────────────────────────────────────────────
#define TEST_VAL      21.0f

FirebaseData   fbData;
FirebaseConfig fbConfig;
FirebaseAuth   fbAuth;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Firebase Push Test ===");

    // WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting");
    while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
    Serial.printf("\n[WiFi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());

    // NTP
    configTime(0, 0, "pool.ntp.org");

    // Firebase
    fbConfig.api_key       = FB_API_KEY;
    fbConfig.database_url  = FB_URL;
    fbAuth.user.email      = FB_EMAIL;
    fbAuth.user.password   = FB_PASS;
    Firebase.begin(&fbConfig, &fbAuth);
    Firebase.reconnectWiFi(true);

    Serial.print("[FB] Authenticating");
    while (!Firebase.ready()) { delay(300); Serial.print("."); }
    Serial.println("\n[FB] Ready");
}

void loop() {
    if (!Firebase.ready()) return;

    FirebaseJson data;
    data.set("temperatureC",  TEST_VAL);
    data.set("humidityPct",   TEST_VAL);
    data.set("smokeRaw",      (int)TEST_VAL);
    data.set("smokePct",      TEST_VAL);
    data.set("distanceCm",    TEST_VAL);
    data.set("riskScore",     0.21f);
    data.set("mlLabel",       0);
    data.set("state",         "MONITORING");
    data.set("ts",            (double)time(nullptr) * 1000.0);

    String latestPath = String("/devices/") + DEVICE_ID + "/latest";
    String tsPath     = String("/readings/") + DEVICE_ID;

    if (Firebase.RTDB.updateNode(&fbData, latestPath.c_str(), &data)) {
        Serial.println("[FB] /devices/esp32_node_01/latest — OK");
    } else {
        Serial.printf("[FB] Failed: %s\n", fbData.errorReason().c_str());
    }

    if (Firebase.RTDB.push(&fbData, tsPath.c_str(), &data)) {
        Serial.println("[FB] /readings/esp32_node_01 — OK");
    } else {
        Serial.printf("[FB] Failed: %s\n", fbData.errorReason().c_str());
    }

    Serial.println("Waiting 5s...\n");
    delay(5000);
}
