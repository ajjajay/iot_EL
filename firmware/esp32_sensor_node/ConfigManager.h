#pragma once
/*
 * ConfigManager.h
 * Loads device configuration from SPIFFS (onboard flash) at /config.json.
 * Falls back to compile-time defaults if the file is missing or corrupt.
 *
 * This lets you flash the same binary to every device and differentiate
 * them by uploading a per-device config.json via the Arduino SPIFFS uploader
 * or OTA config push from Firebase.
 */

#include <Arduino.h>
#include <ArduinoJson.h>

struct DeviceConfig {
    // Identity
    char deviceId[32];
    char location[64];

    // WiFi (overridden by compile-time secrets if absent)
    char wifiSsid[64];
    char wifiPassword[64];

    // Firebase
    char firebaseApiKey[80];
    char firebaseDatabaseUrl[128];
    char firebaseUserEmail[64];
    char firebaseUserPassword[64];

    // Sensor pins
    uint8_t dhtPin;
    uint8_t ldrPin;       // ADC pin (analog)
    uint8_t relayPin;
    uint8_t ledPin;
    uint8_t dhtType;      // 11 or 22

    // Timing
    uint32_t sensorIntervalMs;    // how often to read sensors
    uint32_t firebaseSyncMs;      // how often to push to Firebase
    uint32_t heartbeatIntervalMs; // heartbeat ping interval

    // Thresholds
    float tempWarningC;
    float tempCriticalC;
    float humidityWarningPct;
    uint16_t lightLowThreshold;   // LDR raw ADC value below which = "dark"

    // ML
    float mlRiskThreshold;        // 0.0–1.0; above this → ALERT state
};

class ConfigManager {
public:
    ConfigManager();

    // Load from SPIFFS /config.json; apply defaults for any missing key
    bool load();

    // Save current config back to SPIFFS (e.g. after OTA config update)
    bool save() const;

    const DeviceConfig& cfg() const { return _cfg; }

    // Allow runtime updates (e.g. from Firebase remote config)
    void setThresholds(float tempWarn, float tempCrit, float humWarn, uint16_t lightLow);

private:
    DeviceConfig _cfg;
    void _applyDefaults();
    bool _parseJson(const String& json);
};
