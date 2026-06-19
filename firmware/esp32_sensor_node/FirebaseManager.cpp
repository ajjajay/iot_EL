#include "FirebaseManager.h"
#include <Arduino.h>
#include <time.h>

FirebaseManager::FirebaseManager(const char* apiKey, const char* databaseUrl,
                                  const char* userEmail, const char* userPassword,
                                  const char* deviceId, const char* storageBucket)
    : _deviceId(deviceId), _connected(false), _authenticated(false),
      _queueHead(0), _queueCount(0)
{
    _fbConfig.api_key          = apiKey;
    _fbConfig.database_url     = databaseUrl;
    _fbAuth.user.email         = userEmail;
    _fbAuth.user.password      = userPassword;

    strlcpy(_storageBucket, storageBucket ? storageBucket : "", sizeof(_storageBucket));
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
    meta.set("firmware",  "2.0.0");
    meta.set("registeredAt", (uint32_t)time(nullptr));

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
    data.set("ts",            (double)time(nullptr) * 1000.0);  // Unix ms for JS Date()

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
    if (!Firebase.RTDB.getString(&_fbDataRead, path.c_str())) return false;

    String val = _fbDataRead.stringData();
    if (val == "ON")  { outRelay = RelayState::ON;  return true; }
    if (val == "OFF") { outRelay = RelayState::OFF; return true; }
    return false;
}

void FirebaseManager::sendHeartbeat(DeviceState state) {
    if (!_connected || !Firebase.ready()) return;

    FirebaseJson hb;
    hb.set("ts",       (uint32_t)time(nullptr));
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
                                  float anomalyScore, const char* denyReason) {
    if (!_connected || !Firebase.ready()) {
        Serial.println("[FB] Offline — sign-in log dropped");
        return;
    }

    FirebaseJson data;
    data.set("userId",      userId);
    data.set("userName",    userName);
    data.set("deviceId",    _deviceId);
    data.set("matchScore",  matchScore);
    data.set("success",     success);
    data.set("anomalyScore", anomalyScore);
    data.set("denyReason",  denyReason ? denyReason : "none");
    data.set("ts",          (double)time(nullptr) * 1000.0);

    String path = String("/signins/") + _deviceId;
    Firebase.RTDB.push(&_fbData, path.c_str(), &data);

    Serial.printf("[FB] Sign-in logged: user=%s success=%s score=%.3f deny=%s\n",
                  userId, success ? "Y" : "N", matchScore,
                  denyReason ? denyReason : "none");
}

bool FirebaseManager::checkUserAuthorization(const char* userId) {
    if (!_connected || !Firebase.ready()) {
        // Offline — allow access so device stays operational
        Serial.println("[AUTH] Offline — granting access by default");
        return true;
    }

    char role[16]     = "staff";
    char devices[256] = "";

    // Read role
    String rolePath = String("/users/") + userId + "/role";
    if (Firebase.RTDB.getString(&_fbDataRead, rolePath.c_str())) {
        strlcpy(role, _fbDataRead.stringData().c_str(), sizeof(role));
    }
    Serial.printf("[AUTH] User '%s' role='%s'\n", userId, role);

    // Admin can open any door
    if (strcmp(role, "admin") == 0) return true;

    // Read allowedDevices (comma-separated string, e.g. "esp32_node_01,esp32_node_02")
    String devPath = String("/users/") + userId + "/allowedDevices";
    if (!Firebase.RTDB.getString(&_fbDataRead, devPath.c_str())) {
        Serial.printf("[AUTH] No allowedDevices for '%s' — denying\n", userId);
        return false;
    }
    strlcpy(devices, _fbDataRead.stringData().c_str(), sizeof(devices));
    Serial.printf("[AUTH] allowedDevices='%s' thisDevice='%s'\n", devices, _deviceId);

    // Check _deviceId appears as a full comma-delimited token
    const char* p = devices;
    size_t dlen = strlen(_deviceId);
    while ((p = strstr(p, _deviceId)) != nullptr) {
        bool startOk = (p == devices) || *(p - 1) == ',';
        bool endOk   = *(p + dlen) == '\0' || *(p + dlen) == ',';
        if (startOk && endOk) {
            Serial.printf("[AUTH] '%s' authorized for '%s'\n", userId, _deviceId);
            return true;
        }
        p++;
    }

    Serial.printf("[AUTH] '%s' NOT authorized for device '%s'\n", userId, _deviceId);
    return false;
}

void FirebaseManager::pushEnrollment(const char* userId, const char* name) {
    if (!_connected || !Firebase.ready()) return;

    FirebaseJson data;
    data.set("userId",      userId);
    data.set("name",        name);
    data.set("deviceId",    _deviceId);
    data.set("enrolledAt",  (double)time(nullptr) * 1000.0);

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
    data.set("ts",          (double)time(nullptr) * 1000.0);

    String path = String("/alerts/") + _deviceId;
    Firebase.RTDB.push(&_fbData, path.c_str(), &data);

    Serial.printf("[FB] Alert logged: type=%s user=%s score=%.3f\n",
                  alertType, userId, anomalyScore);
}

bool FirebaseManager::pollEnrollCommand(char* outUserId, uint8_t userIdLen,
                                         char* outName,   uint8_t nameLen) {
    if (!_connected || !Firebase.ready()) return false;

    String pendingPath = _devicePath() + "/commands/enroll/pending";
    if (!Firebase.RTDB.getBool(&_fbDataRead, pendingPath.c_str())) return false;
    if (!_fbDataRead.boolData()) return false;

    // Read userId
    String uidPath = _devicePath() + "/commands/enroll/userId";
    if (!Firebase.RTDB.getString(&_fbDataRead, uidPath.c_str())) return false;
    strlcpy(outUserId, _fbDataRead.stringData().c_str(), userIdLen);

    // Read name
    String namePath = _devicePath() + "/commands/enroll/name";
    if (Firebase.RTDB.getString(&_fbDataRead, namePath.c_str())) {
        strlcpy(outName, _fbDataRead.stringData().c_str(), nameLen);
    } else {
        strlcpy(outName, outUserId, nameLen);  // fall back to userId as name
    }

    // Clear the pending flag (write path — use _fbData)
    Firebase.RTDB.setBool(&_fbData, pendingPath.c_str(), false);

    Serial.printf("[FB] Enroll command: userId=%s name=%s\n", outUserId, outName);
    return true;
}

// ── Firebase Storage upload ───────────────────────────────────────────────────

String FirebaseManager::uploadJpegToStorage(const uint8_t* buf, size_t len,
                                             const String& remotePath) {
    if (!_connected || !Firebase.ready() || _storageBucket[0] == '\0') return "";

    // URL-encode the path: replace '/' with '%2F'
    String encoded = remotePath;
    encoded.replace("/", "%2F");

    String url = String("https://firebasestorage.googleapis.com/v0/b/")
                 + _storageBucket
                 + "/o?uploadType=media&name=" + encoded;

    WiFiClientSecure secureClient;
    secureClient.setInsecure();  // prototype — skip cert pinning

    HTTPClient http;
    http.begin(secureClient, url);
    http.addHeader("Content-Type",   "image/jpeg");
    http.addHeader("Authorization",  String("Bearer ") + Firebase.getToken());
    http.addHeader("Content-Length", String(len));
    http.setTimeout(15000);

    int code = http.POST(const_cast<uint8_t*>(buf), len);
    http.end();

    if (code == 200 || code == 201) {
        Serial.printf("[FB] Storage upload OK: %s (%u B)\n", remotePath.c_str(), len);
        return remotePath;
    }

    Serial.printf("[FB] Storage upload failed: HTTP %d path=%s\n", code, remotePath.c_str());
    return "";
}

// ── Dashboard sign-in poll ────────────────────────────────────────────────────

bool FirebaseManager::pollLatestSignIn(double lastSeenTs, char* outName, uint8_t nameLen,
                                        bool& outSuccess, double& outTs) {
    if (!_connected || !Firebase.ready()) return false;

    String path = String("/signins/") + _deviceId;

    // orderBy("$key") works without a Firebase index — push IDs are time-ordered
    QueryFilter q;
    q.orderBy("$key").limitToLast(1);

    Serial.println("[FB] Polling latest sign-in...");

    if (!Firebase.RTDB.getJSON(&_fbDataSignIn, path.c_str(), &q)) {
        Serial.printf("[FB] pollLatestSignIn failed: %s\n",
                      _fbDataSignIn.errorReason().c_str());
        return false;
    }

    Serial.printf("[FB] Raw JSON: %s\n", _fbDataSignIn.jsonString().c_str());

    FirebaseJson     json;
    FirebaseJsonData tsData, nameData, successData;
    json.setJsonData(_fbDataSignIn.jsonString());

    size_t count = json.iteratorBegin();
    Serial.printf("[FB] Iterator count: %d\n", count);
    if (count == 0) { json.iteratorEnd(); return false; }

    String childKey, childVal;
    int    childType;
    json.iteratorGet(0, childType, childKey, childVal);
    json.iteratorEnd();

    Serial.printf("[FB] Child key: %s  val: %s\n", childKey.c_str(), childVal.c_str());

    FirebaseJson child;
    child.setJsonData(childVal);

    child.get(tsData, "ts");
    if (!tsData.success) {
        Serial.println("[FB] ts field missing");
        return false;
    }

    double ts = tsData.to<double>();
    Serial.printf("[FB] ts=%.0f  lastSeen=%.0f\n", ts, lastSeenTs);
    if (ts <= lastSeenTs) return false;

    outTs = ts;

    child.get(nameData,    "userName");
    child.get(successData, "success");

    const char* name = nameData.success ? nameData.to<const char*>() : "Unknown";
    strlcpy(outName, name, nameLen);
    outSuccess = successData.success ? successData.to<bool>() : false;

    Serial.printf("[FB] New sign-in → %s  success=%s\n",
                  outName, outSuccess ? "Y" : "N");
    return true;
}

void FirebaseManager::requestQrCode() {
    String path = _devicePath() + "/commands/qrRequest";
    FirebaseJson json;
    json.set("pending", true);
    json.set("ts",      (int)(millis()));
    if (Firebase.RTDB.setJSON(&_fbData, path.c_str(), &json)) {
        Serial.println("[FB] QR code request written");
    } else {
        Serial.printf("[FB] QR request failed: %s\n", _fbData.errorReason().c_str());
    }
}

// ── Path helpers ──────────────────────────────────────────────────────────────

String FirebaseManager::_devicePath() const {
    return String("/devices/") + _deviceId;
}

String FirebaseManager::_readingsPath() const {
    return String("/readings/") + _deviceId;
}
