#include "ConfigManager.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

// ── Compile-time fallback credentials (override with config.json) ─────────────
// These values are intentionally empty — fill via config.json on device flash.
#ifndef DEFAULT_WIFI_SSID
  #define DEFAULT_WIFI_SSID     ""
#endif
#ifndef DEFAULT_WIFI_PASS
  #define DEFAULT_WIFI_PASS     ""
#endif
#ifndef DEFAULT_FIREBASE_URL
  #define DEFAULT_FIREBASE_URL  ""
#endif
#ifndef DEFAULT_FIREBASE_KEY
  #define DEFAULT_FIREBASE_KEY  ""
#endif
#ifndef DEFAULT_DEVICE_ID
  #define DEFAULT_DEVICE_ID     "esp32_node_01"
#endif

ConfigManager::ConfigManager() {
    _applyDefaults();
}

void ConfigManager::_applyDefaults() {
    // Identity
    strlcpy(_cfg.deviceId,  DEFAULT_DEVICE_ID,  sizeof(_cfg.deviceId));
    strlcpy(_cfg.location,  "Unset",             sizeof(_cfg.location));

    // WiFi
    strlcpy(_cfg.wifiSsid,     DEFAULT_WIFI_SSID, sizeof(_cfg.wifiSsid));
    strlcpy(_cfg.wifiPassword, DEFAULT_WIFI_PASS,  sizeof(_cfg.wifiPassword));

    // Firebase
    strlcpy(_cfg.firebaseApiKey,      DEFAULT_FIREBASE_KEY, sizeof(_cfg.firebaseApiKey));
    strlcpy(_cfg.firebaseDatabaseUrl, DEFAULT_FIREBASE_URL, sizeof(_cfg.firebaseDatabaseUrl));
    strlcpy(_cfg.firebaseUserEmail,   "",                    sizeof(_cfg.firebaseUserEmail));
    strlcpy(_cfg.firebaseUserPassword,"",                    sizeof(_cfg.firebaseUserPassword));

    // Pins (change to match your wiring)
    _cfg.dhtPin   = 4;
    _cfg.ldrPin   = 34;
    _cfg.relayPin = 26;
    _cfg.ledPin   = 2;
    _cfg.dhtType  = 22;  // DHT22

    // Timing
    _cfg.sensorIntervalMs    = 5000;    // 5 s
    _cfg.firebaseSyncMs      = 10000;   // 10 s
    _cfg.heartbeatIntervalMs = 30000;   // 30 s

    // Thresholds
    _cfg.tempWarningC        = 35.0f;
    _cfg.tempCriticalC       = 45.0f;
    _cfg.humidityWarningPct  = 80.0f;
    _cfg.lightLowThreshold   = 200;

    // ML
    _cfg.mlRiskThreshold = 0.6f;
}

bool ConfigManager::load() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[CFG] SPIFFS mount failed — using defaults");
        return false;
    }

    if (!SPIFFS.exists("/config.json")) {
        Serial.println("[CFG] /config.json not found — using defaults");
        return false;
    }

    File f = SPIFFS.open("/config.json", "r");
    if (!f) {
        Serial.println("[CFG] Cannot open /config.json — using defaults");
        return false;
    }

    String json = f.readString();
    f.close();

    if (!_parseJson(json)) {
        Serial.println("[CFG] JSON parse failed — using defaults");
        return false;
    }

    Serial.printf("[CFG] Loaded config for device '%s' @ '%s'\n",
                  _cfg.deviceId, _cfg.location);
    return true;
}

bool ConfigManager::_parseJson(const String& json) {
    // 1 KB is plenty for our flat config object
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[CFG] JSON error: %s\n", err.c_str());
        return false;
    }

    // Only overwrite fields present in the JSON; keep defaults otherwise
    if (doc.containsKey("deviceId"))       strlcpy(_cfg.deviceId,  doc["deviceId"],  sizeof(_cfg.deviceId));
    if (doc.containsKey("location"))       strlcpy(_cfg.location,  doc["location"],  sizeof(_cfg.location));
    if (doc.containsKey("wifiSsid"))       strlcpy(_cfg.wifiSsid,     doc["wifiSsid"],     sizeof(_cfg.wifiSsid));
    if (doc.containsKey("wifiPassword"))   strlcpy(_cfg.wifiPassword, doc["wifiPassword"],  sizeof(_cfg.wifiPassword));
    if (doc.containsKey("firebaseApiKey")) strlcpy(_cfg.firebaseApiKey, doc["firebaseApiKey"], sizeof(_cfg.firebaseApiKey));
    if (doc.containsKey("firebaseUrl"))    strlcpy(_cfg.firebaseDatabaseUrl, doc["firebaseUrl"], sizeof(_cfg.firebaseDatabaseUrl));
    if (doc.containsKey("firebaseEmail"))  strlcpy(_cfg.firebaseUserEmail, doc["firebaseEmail"], sizeof(_cfg.firebaseUserEmail));
    if (doc.containsKey("firebasePass"))   strlcpy(_cfg.firebaseUserPassword, doc["firebasePass"], sizeof(_cfg.firebaseUserPassword));

    if (doc.containsKey("dhtPin"))   _cfg.dhtPin   = doc["dhtPin"];
    if (doc.containsKey("ldrPin"))   _cfg.ldrPin   = doc["ldrPin"];
    if (doc.containsKey("relayPin")) _cfg.relayPin = doc["relayPin"];
    if (doc.containsKey("ledPin"))   _cfg.ledPin   = doc["ledPin"];
    if (doc.containsKey("dhtType"))  _cfg.dhtType  = doc["dhtType"];

    if (doc.containsKey("sensorIntervalMs"))    _cfg.sensorIntervalMs    = doc["sensorIntervalMs"];
    if (doc.containsKey("firebaseSyncMs"))      _cfg.firebaseSyncMs      = doc["firebaseSyncMs"];
    if (doc.containsKey("heartbeatIntervalMs")) _cfg.heartbeatIntervalMs = doc["heartbeatIntervalMs"];

    if (doc.containsKey("tempWarningC"))       _cfg.tempWarningC       = doc["tempWarningC"];
    if (doc.containsKey("tempCriticalC"))      _cfg.tempCriticalC      = doc["tempCriticalC"];
    if (doc.containsKey("humidityWarningPct")) _cfg.humidityWarningPct = doc["humidityWarningPct"];
    if (doc.containsKey("lightLowThreshold"))  _cfg.lightLowThreshold  = doc["lightLowThreshold"];
    if (doc.containsKey("mlRiskThreshold"))    _cfg.mlRiskThreshold    = doc["mlRiskThreshold"];

    return true;
}

bool ConfigManager::save() const {
    if (!SPIFFS.begin(true)) return false;

    StaticJsonDocument<1024> doc;
    doc["deviceId"]          = _cfg.deviceId;
    doc["location"]          = _cfg.location;
    doc["wifiSsid"]          = _cfg.wifiSsid;
    doc["wifiPassword"]      = _cfg.wifiPassword;
    doc["firebaseApiKey"]    = _cfg.firebaseApiKey;
    doc["firebaseUrl"]       = _cfg.firebaseDatabaseUrl;
    doc["firebaseEmail"]     = _cfg.firebaseUserEmail;
    doc["firebasePass"]      = _cfg.firebaseUserPassword;
    doc["dhtPin"]            = _cfg.dhtPin;
    doc["ldrPin"]            = _cfg.ldrPin;
    doc["relayPin"]          = _cfg.relayPin;
    doc["ledPin"]            = _cfg.ledPin;
    doc["dhtType"]           = _cfg.dhtType;
    doc["sensorIntervalMs"]  = _cfg.sensorIntervalMs;
    doc["firebaseSyncMs"]    = _cfg.firebaseSyncMs;
    doc["heartbeatIntervalMs"] = _cfg.heartbeatIntervalMs;
    doc["tempWarningC"]      = _cfg.tempWarningC;
    doc["tempCriticalC"]     = _cfg.tempCriticalC;
    doc["humidityWarningPct"]= _cfg.humidityWarningPct;
    doc["lightLowThreshold"] = _cfg.lightLowThreshold;
    doc["mlRiskThreshold"]   = _cfg.mlRiskThreshold;

    File f = SPIFFS.open("/config.json", "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    Serial.println("[CFG] Config saved to SPIFFS");
    return true;
}

void ConfigManager::setThresholds(float tempWarn, float tempCrit,
                                   float humWarn, uint16_t lightLow) {
    _cfg.tempWarningC       = tempWarn;
    _cfg.tempCriticalC      = tempCrit;
    _cfg.humidityWarningPct = humWarn;
    _cfg.lightLowThreshold  = lightLow;
    save();
}
