#include "FirebaseManager.h"
#include <Arduino.h>

FirebaseManager::FirebaseManager(const char* apiKey, const char* databaseUrl,
                                  const char* userEmail, const char* userPassword,
                                  const char* deviceId)
    : _deviceId(deviceId), _connected(false), _authenticated(false),
      _queueHead(0), _queueCount(0)
{
    _fbConfig.api_key          = apiKey;
    _fbConfig.database_url     = databaseUrl;
    _fbAuth.user.email         = userEmail;
    _fbAuth.user.password      = userPassword;

    memset(_queue, 0, sizeof(_queue));
}

bool FirebaseManager::begin() {
    Serial.println("[FB] Initialising Firebase...");

    Firebase.begin(&_fbConfig, &_fbAuth);
    Firebase.reconnectWiFi(true);

    // Wait up to 10 s for authentication token
    uint32_t start = millis();
    while (!Firebase.ready() && millis() - start < 10000) {
        Serial.print(".");
        delay(300);
    }
    Serial.println();

    if (!Firebase.ready()) {
        Serial.println("[FB] Auth failed — operating in offline mode");
        return false;
    }

    _authenticated = true;
    _connected     = true;

    // Register device on first connect
    String path = _devicePath() + "/meta";
    FirebaseJson meta;
    meta.set("deviceId",  _deviceId);
    meta.set("firmware",  "1.0.0");
    meta.set("registeredAt", (uint32_t)(millis() / 1000));

    if (!Firebase.RTDB.updateNode(&_fbData, path.c_str(), &meta)) {
        Serial.printf("[FB] Device registration failed: %s\n",
                      _fbData.errorReason().c_str());
    } else {
        Serial.printf("[FB] Device '%s' registered\n", _deviceId);
    }

    setOnline(true);
    return true;
}

void FirebaseManager::pushReading(const SensorReading& s, const MLResult& ml,
                                   DeviceState state) {
    if (!_connected || !Firebase.ready()) {
        Serial.println("[FB] Offline — queuing reading");
        _enqueue(s, ml, state);
        return;
    }

    // Flush any queued readings first (FIFO order)
    if (_queueCount > 0) flushQueue();

    _pushOne(s, ml, state);
}

bool FirebaseManager::_pushOne(const SensorReading& s, const MLResult& ml,
                                DeviceState st) {
    FirebaseJson data;
    data.set("temperatureC",  s.temperatureC);
    data.set("humidityPct",   s.humidityPct);
    data.set("smokeRaw",      s.smokeRaw);
    data.set("smokePct",      s.smokePct);
    data.set("distanceCm",    s.distanceCm);
    data.set("riskScore",     ml.riskScore);
    data.set("mlLabel",       ml.label);
    data.set("pNormal",       ml.pNormal);
    data.set("pWarning",      ml.pWarning);
    data.set("pCritical",     ml.pCritical);
    data.set("state",         stateToString(st));
    data.set("ts",            s.timestampMs);

    // Update latest reading (overwrite) on /devices/{id}/latest
    String latestPath = _devicePath() + "/latest";
    if (!Firebase.RTDB.updateNode(&_fbData, latestPath.c_str(), &data)) {
        Serial.printf("[FB] Push failed: %s\n", _fbData.errorReason().c_str());
        return false;
    }

    // Append to time-series log under /readings/{id}/{pushId}
    String tsPath = _readingsPath();
    if (!Firebase.RTDB.push(&_fbData, tsPath.c_str(), &data)) {
        Serial.printf("[FB] History push failed: %s\n",
                      _fbData.errorReason().c_str());
        // Not fatal — latest was already updated
    }

    Serial.printf("[FB] Pushed: T=%.1f H=%.1f smoke=%.1f%% dist=%.1fcm risk=%.3f\n",
                  s.temperatureC, s.humidityPct, s.smokePct, s.distanceCm, ml.riskScore);
    return true;
}

bool FirebaseManager::pollCommands(RelayState& outRelay) {
    if (!_connected || !Firebase.ready()) return false;

    String path = _devicePath() + "/commands/relayOverride";
    if (!Firebase.RTDB.getString(&_fbData, path.c_str())) return false;

    String val = _fbData.stringData();
    if (val == "ON")  { outRelay = RelayState::ON;  return true; }
    if (val == "OFF") { outRelay = RelayState::OFF; return true; }
    return false;
}

void FirebaseManager::sendHeartbeat(DeviceState state) {
    if (!_connected || !Firebase.ready()) return;

    FirebaseJson hb;
    hb.set("ts",       (uint32_t)(millis() / 1000));
    hb.set("state",    stateToString(state));
    hb.set("heapFree", (uint32_t)ESP.getFreeHeap());
    hb.set("uptime",   (uint32_t)(millis() / 1000));

    String path = _devicePath() + "/heartbeat";
    Firebase.RTDB.updateNode(&_fbData, path.c_str(), &hb);
}

void FirebaseManager::setOnline(bool online) {
    if (!_connected || !Firebase.ready()) return;
    String path = _devicePath() + "/online";
    Firebase.RTDB.setBool(&_fbData, path.c_str(), online);
}

void FirebaseManager::flushQueue() {
    Serial.printf("[FB] Flushing %d queued readings\n", _queueCount);
    uint8_t flushed = 0;
    for (uint8_t i = 0; i < OFFLINE_QUEUE_SIZE && flushed < _queueCount; i++) {
        uint8_t idx = (_queueHead + i) % OFFLINE_QUEUE_SIZE;
        if (!_queue[idx].occupied) continue;
        if (_pushOne(_queue[idx].sensor, _queue[idx].ml, _queue[idx].state)) {
            _queue[idx].occupied = false;
            flushed++;
        } else {
            break;  // stop flushing if Firebase still failing
        }
    }
    _queueCount -= flushed;
    if (flushed > 0) _queueHead = (_queueHead + flushed) % OFFLINE_QUEUE_SIZE;
    Serial.printf("[FB] Flushed %d / remaining %d\n", flushed, _queueCount);
}

void FirebaseManager::_enqueue(const SensorReading& s, const MLResult& ml,
                                DeviceState st) {
    if (_queueCount >= OFFLINE_QUEUE_SIZE) {
        // Drop oldest reading (ring buffer overwrite)
        Serial.println("[FB] Queue full — dropping oldest reading");
        _queueHead = (_queueHead + 1) % OFFLINE_QUEUE_SIZE;
        _queueCount--;
    }
    uint8_t tail = (_queueHead + _queueCount) % OFFLINE_QUEUE_SIZE;
    _queue[tail] = { s, ml, st, true };
    _queueCount++;
}

// ── Biometric logging ─────────────────────────────────────────────────────────

void FirebaseManager::pushSignIn(const char* userId, const char* userName,
                                  float matchScore, bool success,
                                  float anomalyScore) {
    if (!_connected || !Firebase.ready()) {
        Serial.println("[FB] Offline — sign-in log dropped");
        return;
    }

    FirebaseJson data;
    data.set("userId",      userId);
    data.set("userName",    userName);
    data.set("matchScore",  matchScore);
    data.set("success",     success);
    data.set("anomalyScore", anomalyScore);
    data.set("ts",          (uint32_t)(millis() / 1000));

    String path = String("/signins/") + _deviceId;
    Firebase.RTDB.push(&_fbData, path.c_str(), &data);

    Serial.printf("[FB] Sign-in logged: user=%s success=%s score=%.3f\n",
                  userId, success ? "Y" : "N", matchScore);
}

void FirebaseManager::pushEnrollment(const char* userId, const char* name) {
    if (!_connected || !Firebase.ready()) return;

    FirebaseJson data;
    data.set("userId",      userId);
    data.set("name",        name);
    data.set("deviceId",    _deviceId);
    data.set("enrolledAt",  (uint32_t)(millis() / 1000));

    String path = String("/users/") + userId;
    Firebase.RTDB.updateNode(&_fbData, path.c_str(), &data);

    Serial.printf("[FB] Enrollment logged: user=%s name=%s\n", userId, name);
}

void FirebaseManager::pushBiometricAlert(const char* userId,
                                          const char* alertType,
                                          float anomalyScore) {
    if (!_connected || !Firebase.ready()) return;

    FirebaseJson data;
    data.set("deviceId",    _deviceId);
    data.set("userId",      userId);
    data.set("alertType",   alertType);
    data.set("anomalyScore", anomalyScore);
    data.set("acknowledged", false);
    data.set("ts",          (uint32_t)(millis() / 1000));

    String path = String("/alerts/") + _deviceId;
    Firebase.RTDB.push(&_fbData, path.c_str(), &data);

    Serial.printf("[FB] Alert logged: type=%s user=%s score=%.3f\n",
                  alertType, userId, anomalyScore);
}

bool FirebaseManager::pollEnrollCommand(char* outUserId, uint8_t userIdLen,
                                         char* outName,   uint8_t nameLen) {
    if (!_connected || !Firebase.ready()) return false;

    String pendingPath = _devicePath() + "/commands/enroll/pending";
    if (!Firebase.RTDB.getBool(&_fbData, pendingPath.c_str())) return false;
    if (!_fbData.boolData()) return false;

    // Read userId
    String uidPath = _devicePath() + "/commands/enroll/userId";
    if (!Firebase.RTDB.getString(&_fbData, uidPath.c_str())) return false;
    strlcpy(outUserId, _fbData.stringData().c_str(), userIdLen);

    // Read name
    String namePath = _devicePath() + "/commands/enroll/name";
    if (Firebase.RTDB.getString(&_fbData, namePath.c_str())) {
        strlcpy(outName, _fbData.stringData().c_str(), nameLen);
    } else {
        strlcpy(outName, outUserId, nameLen);  // fall back to userId as name
    }

    // Clear the pending flag so it doesn't trigger again
    Firebase.RTDB.setBool(&_fbData, pendingPath.c_str(), false);

    Serial.printf("[FB] Enroll command: userId=%s name=%s\n", outUserId, outName);
    return true;
}

// ── Path helpers ──────────────────────────────────────────────────────────────

String FirebaseManager::_devicePath() const {
    return String("/devices/") + _deviceId;
}

String FirebaseManager::_readingsPath() const {
    return String("/readings/") + _deviceId;
}
