#include "AWSIoTManager.h"
#include <ArduinoJson.h>

AWSIoTManager* AWSIoTManager::_instance = nullptr;

AWSIoTManager::AWSIoTManager(const char* endpoint, const char* thingName,
                               const char* rootCA, const char* deviceCert,
                               const char* privateKey)
    : _mqtt(_wifiClient), _endpoint(endpoint), _thingName(thingName),
      _relayPending(false), _agentAckPending(false)
{
    _agentAckUserId[0] = '\0';
    _agentAckType[0]   = '\0';
    _instance = this;
    _wifiClient.setCACert(rootCA);
    _wifiClient.setCertificate(deviceCert);
    _wifiClient.setPrivateKey(privateKey);
}

bool AWSIoTManager::begin() {
    Serial.printf("[AWS] Connecting to AWS IoT Core: %s\n", _endpoint);
    _mqtt.setServer(_endpoint, AWS_MQTT_PORT);
    _mqtt.setBufferSize(AWS_MQTT_BUFFER);
    _mqtt.setCallback(_staticCallback);
    return _reconnect();
}

void AWSIoTManager::loop() {
    if (!_mqtt.connected()) {
        _reconnect();
    }
    _mqtt.loop();
}

bool AWSIoTManager::_reconnect() {
    if (_mqtt.connected()) return true;

    // Client ID must be unique per connection; use thing name as identifier
    String clientId = String("esp32-") + _thingName + "-" + String(millis());

    Serial.print("[AWS] MQTT connect...");
    if (!_mqtt.connect(clientId.c_str())) {
        Serial.printf(" failed (rc=%d)\n", _mqtt.state());
        return false;
    }

    Serial.println(" connected");

    // Subscribe to command, shadow delta, and AI agent ACK topics
    String cmdTopic    = String("iot/") + _thingName + "/commands";
    String shadowDelta = String("$aws/things/") + _thingName + "/shadow/update/delta";
    String aiAlerts    = String("iot/") + _thingName + "/ai/alerts";
    _mqtt.subscribe(cmdTopic.c_str());
    _mqtt.subscribe(shadowDelta.c_str());
    _mqtt.subscribe(aiAlerts.c_str());

    Serial.printf("[AWS] Subscribed to commands, shadow/delta, ai/alerts\n");

    // Also publish to register enrollment topic with IoT Core
    // (subscription not needed — Lambda publishes ACK on ai/alerts)
    return true;
}

void AWSIoTManager::publishReading(const SensorReading& s, const MLResult& ml,
                                    DeviceState state) {
    if (!_mqtt.connected()) return;

    StaticJsonDocument<512> doc;
    doc["deviceId"]     = _thingName;
    doc["temperatureC"] = s.temperatureC;
    doc["humidityPct"]  = s.humidityPct;
    doc["smokeRaw"]     = s.smokeRaw;
    doc["smokePct"]     = s.smokePct;
    doc["distanceCm"]   = s.distanceCm;
    doc["riskScore"]    = ml.riskScore;
    doc["mlLabel"]      = ml.label;
    doc["pNormal"]      = ml.pNormal;
    doc["pWarning"]     = ml.pWarning;
    doc["pCritical"]    = ml.pCritical;
    doc["state"]        = stateToString(state);
    doc["ts"]           = s.timestampMs;

    char buf[512];
    size_t len = serializeJson(doc, buf);

    String topic = String("iot/") + _thingName + "/telemetry";
    if (_mqtt.publish(topic.c_str(), buf, len)) {
        Serial.printf("[AWS] Published: T=%.1f H=%.1f risk=%.3f\n",
                      s.temperatureC, s.humidityPct, ml.riskScore);
    } else {
        Serial.println("[AWS] Publish failed");
    }

    _publishShadow(s, ml, state);
}

void AWSIoTManager::_publishShadow(const SensorReading& s, const MLResult& ml,
                                    DeviceState state) {
    // Device Shadow: report current state so AWS IoT knows the device's reality
    StaticJsonDocument<512> doc;
    JsonObject reported = doc.createNestedObject("state").createNestedObject("reported");
    reported["temperatureC"] = s.temperatureC;
    reported["humidityPct"]  = s.humidityPct;
    reported["smokePct"]     = s.smokePct;
    reported["distanceCm"]   = s.distanceCm;
    reported["riskScore"]    = ml.riskScore;
    reported["mlLabel"]      = ml.label;
    reported["state"]        = stateToString(state);
    reported["ts"]           = s.timestampMs;

    char buf[512];
    size_t len = serializeJson(doc, buf);

    String shadowTopic = String("$aws/things/") + _thingName + "/shadow/update";
    _mqtt.publish(shadowTopic.c_str(), buf, len);
}

void AWSIoTManager::publishHeartbeat(DeviceState state) {
    if (!_mqtt.connected()) return;

    StaticJsonDocument<192> doc;
    doc["deviceId"] = _thingName;
    doc["state"]    = stateToString(state);
    doc["heapFree"] = (uint32_t)ESP.getFreeHeap();
    doc["uptime"]   = (uint32_t)(millis() / 1000);

    char buf[192];
    size_t len = serializeJson(doc, buf);

    String topic = String("iot/") + _thingName + "/heartbeat";
    _mqtt.publish(topic.c_str(), buf, len);
}

bool AWSIoTManager::getRelayCommand(RelayState& outRelay) {
    if (!_relayPending) return false;
    outRelay      = _pendingRelay;
    _relayPending = false;
    return true;
}

bool AWSIoTManager::isConnected() const {
    return _mqtt.connected();
}

// ── Biometric methods ─────────────────────────────────────────────────────────

void AWSIoTManager::publishBiometricEvent(const char* userId,
                                           float matchScore, bool success,
                                           const char* storagePath) {
    if (!_mqtt.connected()) return;

    StaticJsonDocument<384> doc;
    doc["deviceId"]    = _thingName;
    doc["userId"]      = userId;
    doc["matchScore"]  = matchScore;
    doc["success"]     = success;
    doc["storagePath"] = storagePath ? storagePath : "";
    doc["ts"]          = (double)time(nullptr) * 1000.0;

    char buf[384];
    size_t len = serializeJson(doc, buf);

    String topic = String("iot/") + _thingName + "/biometric/signin";
    if (!_mqtt.publish(topic.c_str(), buf, len)) {
        Serial.println("[AWS] Biometric event publish failed");
    } else {
        Serial.printf("[AWS] Biometric event: user=%s success=%s score=%.3f path=%s\n",
                      userId, success ? "Y" : "N", matchScore,
                      storagePath ? storagePath : "none");
    }
}

void AWSIoTManager::publishEnrollmentEvent(const char* userId, const char* name,
                                            const char* storagePath) {
    if (!_mqtt.connected()) return;

    StaticJsonDocument<384> doc;
    doc["deviceId"]    = _thingName;
    doc["userId"]      = userId;
    doc["name"]        = name;
    doc["storagePath"] = storagePath ? storagePath : "";
    doc["ts"]          = (double)time(nullptr) * 1000.0;

    char buf[384];
    size_t len = serializeJson(doc, buf);

    String topic = String("iot/") + _thingName + "/biometric/enroll";
    if (!_mqtt.publish(topic.c_str(), buf, len)) {
        Serial.println("[AWS] Enrollment event publish failed");
    } else {
        Serial.printf("[AWS] Enrollment event: user=%s name=%s path=%s\n",
                      userId, name, storagePath ? storagePath : "none");
    }
}

bool AWSIoTManager::publishAlertJson(const String& alertJson) {
    if (!_mqtt.connected()) return false;

    String topic = String("iot/") + _thingName + "/biometric/alert";
    bool ok = _mqtt.publish(topic.c_str(),
                            (const uint8_t*)alertJson.c_str(), alertJson.length());
    if (!ok) Serial.println("[AWS] Alert publish failed (buffer full?)");
    return ok;
}

bool AWSIoTManager::getAgentAck(char* outUserId, char* outAlertType) {
    if (!_agentAckPending) return false;
    strlcpy(outUserId,    _agentAckUserId, 32);
    strlcpy(outAlertType, _agentAckType,   32);
    _agentAckPending  = false;
    _agentAckUserId[0] = '\0';
    _agentAckType[0]   = '\0';
    return true;
}

// ── Static callback trampoline ─────────────────────────────────────────────────

void AWSIoTManager::_staticCallback(char* topic, byte* payload, unsigned int len) {
    if (_instance) _instance->_onMessage(topic, payload, len);
}

void AWSIoTManager::_onMessage(char* topic, byte* payload, unsigned int len) {
    String topicStr(topic);
    Serial.printf("[AWS] Incoming message on: %s\n", topic);

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload, len) != DeserializationError::Ok) {
        Serial.println("[AWS] Failed to parse incoming JSON");
        return;
    }

    // Command topic: iot/{thingName}/commands  { "relayOverride": "ON"|"OFF"|"AUTO" }
    String cmdTopic = String("iot/") + _thingName + "/commands";
    if (topicStr == cmdTopic) {
        const char* relay = doc["relayOverride"];
        if (!relay) return;
        if (strcmp(relay, "ON")  == 0) { _pendingRelay = RelayState::ON;  _relayPending = true; }
        if (strcmp(relay, "OFF") == 0) { _pendingRelay = RelayState::OFF; _relayPending = true; }
        Serial.printf("[AWS] Relay command received: %s\n", relay);
    }

    // Shadow delta: $aws/things/{thingName}/shadow/update/delta
    String shadowDelta = String("$aws/things/") + _thingName + "/shadow/update/delta";
    if (topicStr == shadowDelta) {
        const char* relay = doc["state"]["relayOverride"];
        if (!relay) return;
        if (strcmp(relay, "ON")  == 0) { _pendingRelay = RelayState::ON;  _relayPending = true; }
        if (strcmp(relay, "OFF") == 0) { _pendingRelay = RelayState::OFF; _relayPending = true; }
        Serial.printf("[AWS] Shadow delta relay: %s\n", relay);
    }

    // AI agent ACK: iot/{thingName}/ai/alerts
    // Bedrock Agent publishes here after delivering user notification.
    // Payload: { "userId": "...", "alertType": "...", "ack": true }
    String aiAlerts = String("iot/") + _thingName + "/ai/alerts";
    if (topicStr == aiAlerts) {
        bool ack = doc["ack"] | false;
        if (!ack) return;
        const char* uid  = doc["userId"]    | "";
        const char* type = doc["alertType"] | "";
        strlcpy(_agentAckUserId, uid,  sizeof(_agentAckUserId));
        strlcpy(_agentAckType,   type, sizeof(_agentAckType));
        _agentAckPending = true;
        Serial.printf("[AWS] Agent ACK received: user=%s type=%s\n", uid, type);
    }
}
