#include "BiometricManager.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <math.h>

static const char* REGISTRY_PATH = "/bio/users.json";

BiometricManager::BiometricManager() : _userCount(0) {
    memset(_users,         0, sizeof(_users));
    memset(_templateCount, 0, sizeof(_templateCount));
    memset(_templates,     0, sizeof(_templates));
}

bool BiometricManager::begin() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[BIO] SPIFFS mount failed");
        return false;
    }
    _loadAll();
    Serial.printf("[BIO] Ready — %d user(s) enrolled\n", _userCount);
    return true;
}

bool BiometricManager::enroll(const char* userId, const char* name,
                               const IrisCapture& iris) {
    if (!iris.valid) {
        Serial.println("[BIO] enroll() rejected: invalid capture");
        return false;
    }

    int idx = _findUser(userId);
    if (idx < 0) {
        if (_userCount >= BIO_MAX_USERS) {
            Serial.println("[BIO] Max users reached");
            return false;
        }
        idx = _userCount++;
        strlcpy(_users[idx].userId, userId, sizeof(_users[idx].userId));
        strlcpy(_users[idx].name,   name,   sizeof(_users[idx].name));
        _users[idx].active        = true;
        _users[idx].templateCount = 0;
        _templateCount[idx]       = 0;
    }

    // Overwrite oldest template when at capacity (ring-buffer)
    uint8_t tIdx = _templateCount[idx] % BIO_MAX_TEMPLATES;
    memcpy(_templates[idx][tIdx], iris.features, sizeof(float) * IRIS_FEAT_DIM);

    if (!_saveTemplate(idx, tIdx)) {
        Serial.printf("[BIO] Failed to save template %d for '%s'\n", tIdx, userId);
        return false;
    }

    if (_templateCount[idx] < BIO_MAX_TEMPLATES) _templateCount[idx]++;
    _users[idx].templateCount = _templateCount[idx];

    saveRegistry();
    Serial.printf("[BIO] Enrolled '%s' template %d/%d\n",
                  userId, tIdx + 1, _templateCount[idx]);
    return true;
}

MatchResult BiometricManager::match(const IrisCapture& iris, float threshold) const {
    MatchResult best;
    best.matched     = false;
    best.score       = 1e9f;
    best.templateIdx = 0;
    best.userId[0]   = '\0';
    best.userName[0] = '\0';

    if (!iris.valid || _userCount == 0) return best;

    for (uint8_t u = 0; u < _userCount; u++) {
        if (!_users[u].active || _templateCount[u] == 0) continue;
        for (uint8_t t = 0; t < _templateCount[u]; t++) {
            float d = _rms(iris.features, _templates[u][t]);
            if (d < best.score) {
                best.score       = d;
                best.templateIdx = t;
                strlcpy(best.userId,   _users[u].userId, sizeof(best.userId));
                strlcpy(best.userName, _users[u].name,   sizeof(best.userName));
            }
        }
    }

    best.matched = (best.score < threshold);
    Serial.printf("[BIO] Match: %s score=%.4f thresh=%.4f → %s\n",
                  best.userId, best.score, threshold,
                  best.matched ? "PASS" : "FAIL");
    return best;
}

bool BiometricManager::isEnrolled(const char* userId) const {
    return _findUser(userId) >= 0;
}

bool BiometricManager::removeUser(const char* userId) {
    int idx = _findUser(userId);
    if (idx < 0) return false;

    for (uint8_t t = 0; t < BIO_MAX_TEMPLATES; t++) {
        String p = _tPath(idx, t);
        if (SPIFFS.exists(p)) SPIFFS.remove(p);
    }

    _users[idx].active        = false;
    _users[idx].templateCount = 0;
    _templateCount[idx]       = 0;

    saveRegistry();
    Serial.printf("[BIO] Removed user '%s'\n", userId);
    return true;
}

bool BiometricManager::saveRegistry() const {
    StaticJsonDocument<2048> doc;
    JsonArray arr = doc.createNestedArray("users");

    for (uint8_t i = 0; i < _userCount; i++) {
        if (!_users[i].active) continue;
        JsonObject o = arr.createNestedObject();
        o["id"]     = _users[i].userId;
        o["name"]   = _users[i].name;
        o["count"]  = _templateCount[i];
        o["active"] = true;
    }

    File f = SPIFFS.open(REGISTRY_PATH, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}

// ── Private ──────────────────────────────────────────────────────────────────

bool BiometricManager::_loadAll() {
    if (!SPIFFS.exists(REGISTRY_PATH)) return true;  // no users yet — OK

    File f = SPIFFS.open(REGISTRY_PATH, "r");
    if (!f) return false;

    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[BIO] Registry parse error: %s\n", err.c_str());
        return false;
    }

    _userCount = 0;
    for (JsonObject u : doc["users"].as<JsonArray>()) {
        if (_userCount >= BIO_MAX_USERS) break;
        uint8_t idx = _userCount++;
        strlcpy(_users[idx].userId, u["id"]   | "", sizeof(_users[idx].userId));
        strlcpy(_users[idx].name,   u["name"] | "", sizeof(_users[idx].name));
        _users[idx].templateCount = u["count"]  | (uint8_t)0;
        _users[idx].active        = u["active"] | false;
        _templateCount[idx]       = 0;
        for (uint8_t t = 0; t < _users[idx].templateCount; t++) _loadTemplate(idx, t);
    }
    return true;
}

int BiometricManager::_findUser(const char* userId) const {
    for (uint8_t i = 0; i < _userCount; i++)
        if (_users[i].active && strcmp(_users[i].userId, userId) == 0) return i;
    return -1;
}

float BiometricManager::_rms(const float* a, const float* b) const {
    float sum = 0.0f;
    for (uint8_t i = 0; i < IRIS_FEAT_DIM; i++) {
        float d = a[i] - b[i]; sum += d * d;
    }
    return sqrtf(sum / IRIS_FEAT_DIM);
}

bool BiometricManager::_saveTemplate(uint8_t ui, uint8_t ti) const {
    File f = SPIFFS.open(_tPath(ui, ti), "w");
    if (!f) return false;
    f.write((const uint8_t*)_templates[ui][ti], sizeof(float) * IRIS_FEAT_DIM);
    f.close();
    return true;
}

bool BiometricManager::_loadTemplate(uint8_t ui, uint8_t ti) {
    String p = _tPath(ui, ti);
    if (!SPIFFS.exists(p)) return false;
    File f = SPIFFS.open(p, "r");
    if (!f) return false;
    size_t n = f.read((uint8_t*)_templates[ui][ti], sizeof(float) * IRIS_FEAT_DIM);
    f.close();
    if (n == sizeof(float) * IRIS_FEAT_DIM) { _templateCount[ui]++; return true; }
    return false;
}

String BiometricManager::_tPath(uint8_t ui, uint8_t ti) const {
    return String("/bio/") + _users[ui].userId + "_t" + ti + ".bin";
}
