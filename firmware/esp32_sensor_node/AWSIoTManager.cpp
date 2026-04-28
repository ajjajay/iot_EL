#include "AWSIoTManager.h"
#include <ArduinoJson.h>

AWSIoTManager* AWSIoTManager::_instance = nullptr;

AWSIoTManager::AWSIoTManager(const char* endpoint, const char* thingName,
                               const char* rootCA, const char* deviceCert,
                               const char* privateKey)
    : _mqtt(_wifiClient), _endpoint(endpoint), _thingName(thingName),
      _relayPending(false)
{
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

    // Subscribe to command and shadow delta topics
    String cmdTopic    = String("iot/") + _thingName + "/commands";
    String shadowDelta = String("$aws/things/") + _thingName + "/shadow/update/delta";
    _mqtt.subscribe(cmdTopic.c_str());
    _mqtt.subscribe(shadowDelta.c_str());

    Serial.printf("[AWS] Subscribed to %s and shadow/delta\n", cmdTopic.c_str());
    return true;
}

void AWSIoTManager::publishReading(const SensorReading& s, const MLResult& ml,
                                    DeviceState state) {
    if (!_mqtt.connected()) return;

    StaticJsonDocument<512> doc;
    doc["deviceId"]     = _thingName;
    doc["temperatureC"] = s.temperatureC;
    doc["humidityPct"]  = s.humidityPct;
    doc["lightRaw"]     = s.lightRaw;
    doc["lightNorm"]    = s.lightNorm;
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
    reported["lightNorm"]    = s.lightNorm;
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
    // AWS sends this when the desired state differs from the reported state.
    // Payload: { "state": { "relayOverride": "ON"|"OFF"|"AUTO" } }
    String shadowDelta = String("$aws/things/") + _thingName + "/shadow/update/delta";
    if (topicStr == shadowDelta) {
        const char* relay = doc["state"]["relayOverride"];
        if (!relay) return;
        if (strcmp(relay, "ON")  == 0) { _pendingRelay = RelayState::ON;  _relayPending = true; }
        if (strcmp(relay, "OFF") == 0) { _pendingRelay = RelayState::OFF; _relayPending = true; }
        Serial.printf("[AWS] Shadow delta relay: %s\n", relay);
    }
}
