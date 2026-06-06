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
    strlcpy(_cfg.firebaseUserEmail,      "",                              sizeof(_cfg.firebaseUserEmail));
    strlcpy(_cfg.firebaseUserPassword,   "",                              sizeof(_cfg.firebaseUserPassword));
    strlcpy(_cfg.firebaseStorageBucket,  "iot-fc8b3.firebasestorage.app", sizeof(_cfg.firebaseStorageBucket));

    // Sensor pins
    _cfg.dhtPin          = 4;
    _cfg.dhtType         = 11;   // DHT11
    _cfg.smokePin        = 32;   // MQ-2 analog out
    _cfg.ultrasonicTrig  = 5;    // HC-SR04 TRIG
    _cfg.ultrasonicEcho  = 18;   // HC-SR04 ECHO

    // Timing
    _cfg.sensorIntervalMs    = 5000;    // 5 s
    _cfg.firebaseSyncMs      = 10000;   // 10 s
    _cfg.heartbeatIntervalMs = 30000;   // 30 s

    // Thresholds
    _cfg.tempWarningC        = 35.0f;
    _cfg.tempCriticalC       = 45.0f;
    _cfg.humidityWarningPct  = 80.0f;
    _cfg.lightLowThreshold   = 200;

    // ML (environmental)
    _cfg.mlRiskThreshold = 0.6f;

    // AWS IoT Core
    strlcpy(_cfg.awsEndpoint,  "", sizeof(_cfg.awsEndpoint));
    strlcpy(_cfg.awsThingName, "", sizeof(_cfg.awsThingName));
    _cfg.awsEnabled = false;

    // Biometric / Iris (only relevant when cameraEnabled = true)
    _cfg.cameraEnabled      = false;
    _cfg.irisMatchThreshold = 0.30f;
    _cfg.irisEnrollFrames   = 5;
    _cfg.authDisplayMs      = 3000;

    // Keypad PIN
    strlcpy(_cfg.accessPin, "1234", sizeof(_cfg.accessPin));

    // Anomaly detection
    _cfg.anomalyScoreThreshold = 0.60f;
    _cfg.alertCooldownMs       = 30000UL;
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
    StaticJsonDocument<1536> doc;
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
    if (doc.containsKey("firebasePass"))          strlcpy(_cfg.firebaseUserPassword,  doc["firebasePass"],          sizeof(_cfg.firebaseUserPassword));
    if (doc.containsKey("firebaseStorageBucket")) strlcpy(_cfg.firebaseStorageBucket, doc["firebaseStorageBucket"], sizeof(_cfg.firebaseStorageBucket));

    if (doc.containsKey("dhtPin"))          _cfg.dhtPin         = doc["dhtPin"];
    if (doc.containsKey("dhtType"))         _cfg.dhtType        = doc["dhtType"];
    if (doc.containsKey("smokePin"))        _cfg.smokePin       = doc["smokePin"];
    if (doc.containsKey("ultrasonicTrig"))  _cfg.ultrasonicTrig = doc["ultrasonicTrig"];
    if (doc.containsKey("ultrasonicEcho"))  _cfg.ultrasonicEcho = doc["ultrasonicEcho"];

    if (doc.containsKey("sensorIntervalMs"))    _cfg.sensorIntervalMs    = doc["sensorIntervalMs"];
    if (doc.containsKey("firebaseSyncMs"))      _cfg.firebaseSyncMs      = doc["firebaseSyncMs"];
    if (doc.containsKey("heartbeatIntervalMs")) _cfg.heartbeatIntervalMs = doc["heartbeatIntervalMs"];

    if (doc.containsKey("tempWarningC"))       _cfg.tempWarningC       = doc["tempWarningC"];
    if (doc.containsKey("tempCriticalC"))      _cfg.tempCriticalC      = doc["tempCriticalC"];
    if (doc.containsKey("humidityWarningPct")) _cfg.humidityWarningPct = doc["humidityWarningPct"];
    if (doc.containsKey("lightLowThreshold"))  _cfg.lightLowThreshold  = doc["lightLowThreshold"];
    if (doc.containsKey("mlRiskThreshold"))    _cfg.mlRiskThreshold    = doc["mlRiskThreshold"];

    if (doc.containsKey("awsEndpoint"))  strlcpy(_cfg.awsEndpoint,  doc["awsEndpoint"],  sizeof(_cfg.awsEndpoint));
    if (doc.containsKey("awsThingName")) strlcpy(_cfg.awsThingName, doc["awsThingName"], sizeof(_cfg.awsThingName));
    if (doc.containsKey("awsEnabled"))   _cfg.awsEnabled = doc["awsEnabled"];

    // Biometric
    if (doc.containsKey("cameraEnabled"))      _cfg.cameraEnabled      = doc["cameraEnabled"];
    if (doc.containsKey("irisMatchThreshold")) _cfg.irisMatchThreshold = doc["irisMatchThreshold"];
    if (doc.containsKey("irisEnrollFrames"))   _cfg.irisEnrollFrames   = doc["irisEnrollFrames"];
    if (doc.containsKey("authDisplayMs"))      _cfg.authDisplayMs      = doc["authDisplayMs"];

    // Keypad PIN
    if (doc.containsKey("accessPin"))
        strlcpy(_cfg.accessPin, doc["accessPin"] | "1234", sizeof(_cfg.accessPin));

    // Anomaly
    if (doc.containsKey("anomalyScoreThreshold")) _cfg.anomalyScoreThreshold = doc["anomalyScoreThreshold"];
    if (doc.containsKey("alertCooldownMs"))        _cfg.alertCooldownMs       = doc["alertCooldownMs"];

    return true;
}

bool ConfigManager::save() const {
    if (!SPIFFS.begin(true)) return false;

    StaticJsonDocument<2048> doc;
    doc["deviceId"]          = _cfg.deviceId;
    doc["location"]          = _cfg.location;
    doc["wifiSsid"]          = _cfg.wifiSsid;
    doc["wifiPassword"]      = _cfg.wifiPassword;
    doc["firebaseApiKey"]    = _cfg.firebaseApiKey;
    doc["firebaseUrl"]       = _cfg.firebaseDatabaseUrl;
    doc["firebaseEmail"]     = _cfg.firebaseUserEmail;
    doc["firebasePass"]      = _cfg.firebaseUserPassword;
    doc["dhtPin"]            = _cfg.dhtPin;
    doc["dhtType"]           = _cfg.dhtType;
    doc["smokePin"]          = _cfg.smokePin;
    doc["ultrasonicTrig"]    = _cfg.ultrasonicTrig;
    doc["ultrasonicEcho"]    = _cfg.ultrasonicEcho;
    doc["sensorIntervalMs"]  = _cfg.sensorIntervalMs;
    doc["firebaseSyncMs"]    = _cfg.firebaseSyncMs;
    doc["heartbeatIntervalMs"] = _cfg.heartbeatIntervalMs;
    doc["tempWarningC"]      = _cfg.tempWarningC;
    doc["tempCriticalC"]     = _cfg.tempCriticalC;
    doc["humidityWarningPct"]= _cfg.humidityWarningPct;
    doc["lightLowThreshold"] = _cfg.lightLowThreshold;
    doc["mlRiskThreshold"]   = _cfg.mlRiskThreshold;
    doc["awsEndpoint"]       = _cfg.awsEndpoint;
    doc["awsThingName"]      = _cfg.awsThingName;
    doc["awsEnabled"]        = _cfg.awsEnabled;

    doc["cameraEnabled"]         = _cfg.cameraEnabled;
    doc["irisMatchThreshold"]    = _cfg.irisMatchThreshold;
    doc["irisEnrollFrames"]      = _cfg.irisEnrollFrames;
    doc["authDisplayMs"]         = _cfg.authDisplayMs;
    doc["anomalyScoreThreshold"] = _cfg.anomalyScoreThreshold;
    doc["alertCooldownMs"]       = _cfg.alertCooldownMs;
    doc["accessPin"]             = _cfg.accessPin;

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
