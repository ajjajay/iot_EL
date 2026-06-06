#pragma once
/*
 * AWSIoTManager.h
 * MQTT bridge to AWS IoT Core.
 *
 * Runs alongside FirebaseManager as a parallel publish channel.
 * Uses X.509 mutual-TLS on port 8883 via WiFiClientSecure + PubSubClient.
 *
 * Topics (all prefixed with "iot/{thingName}/"):
 *   PUBLISH  iot/{thingName}/telemetry    — sensor + ML reading each cycle
 *   PUBLISH  iot/{thingName}/heartbeat    — periodic alive ping
 *   SUBSCRIBE iot/{thingName}/commands    — relay override from any MQTT client
 *   PUBLISH/SUBSCRIBE $aws/things/{thingName}/shadow/update   — Device Shadow
 *   SUBSCRIBE $aws/things/{thingName}/shadow/update/delta     — desired changes
 *
 * Required Arduino library: PubSubClient (Nick O'Leary) — install via Library Manager
 */

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "SensorManager.h"
#include "MLInference.h"
#include "StateManager.h"
#include "ActuatorController.h"

// MQTT broker port for TLS (standard AWS IoT Core port)
static constexpr uint16_t AWS_MQTT_PORT = 8883;

// JSON payload buffer — must be larger than the largest message we publish
static constexpr uint16_t AWS_MQTT_BUFFER = 1024;

class AWSIoTManager {
public:
    AWSIoTManager(const char* endpoint, const char* thingName,
                  const char* rootCA, const char* deviceCert, const char* privateKey);

    // Call once in setup() after WiFi connects
    bool begin();

    // Call every loop() iteration to service MQTT keep-alive and incoming messages
    void loop();

    // Publish full sensor+ML reading to iot/{thingName}/telemetry and Device Shadow
    void publishReading(const SensorReading& s, const MLResult& ml, DeviceState state);

    // Publish heartbeat ping to iot/{thingName}/heartbeat
    void publishHeartbeat(DeviceState state);

    // Returns true (once) if a relay command arrived via MQTT since last call
    bool getRelayCommand(RelayState& outRelay);

    // ── Biometric events ─────────────────────────────────────────────────────

    // Publish sign-in result to iot/{thingName}/biometric/signin
    // storagePath is the Firebase Storage path to the captured JPEG (for Rekognition)
    void publishBiometricEvent(const char* userId, float matchScore, bool success,
                               const char* storagePath = "");

    // Publish enrollment event to iot/{thingName}/biometric/enroll
    // Lambda picks this up and indexes the face in Rekognition collection
    void publishEnrollmentEvent(const char* userId, const char* name,
                                const char* storagePath);

    // Publish anomaly alert JSON (pre-formatted by AlertManager) to
    // iot/{thingName}/biometric/alert → IoT Rule → Lambda → Bedrock Agent
    bool publishAlertJson(const String& alertJson);

    // Returns true (once) when the Bedrock Agent sends an ACK on
    // iot/{thingName}/ai/alerts  { "userId": "...", "alertType": "...", "ack": true }
    bool getAgentAck(char* outUserId, char* outAlertType);

    bool isConnected() const;

private:
    WiFiClientSecure       _wifiClient;
    mutable PubSubClient   _mqtt;

    const char* _endpoint;
    const char* _thingName;

    bool       _relayPending;
    RelayState _pendingRelay;

    bool _agentAckPending;
    char _agentAckUserId[32];
    char _agentAckType[32];

    bool _reconnect();
    void _onMessage(char* topic, byte* payload, unsigned int len);
    void _publishShadow(const SensorReading& s, const MLResult& ml, DeviceState state);

    // Static trampoline for PubSubClient callback (PubSubClient requires a plain function ptr)
    static AWSIoTManager* _instance;
    static void _staticCallback(char* topic, byte* payload, unsigned int len);
};
