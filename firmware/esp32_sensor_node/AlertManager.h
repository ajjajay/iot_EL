#pragma once
/*
 * AlertManager.h
 * Composes and throttles biometric anomaly alerts.
 *
 * Responsibilities:
 *   - Format JSON alert payloads
 *   - Enforce per-type cooldown so the same alert type does not storm
 *   - Publish to AWS IoT MQTT topic via AWSIoTManager
 *   - Write to Firebase /alerts/{deviceId}/ via FirebaseManager
 *
 * AWS IoT routing (configure in IoT Core Rule Engine):
 *   Topic  : iot/{thingName}/biometric/alert
 *   Action : Lambda → Bedrock Agent → SNS/email/push notification to user
 *
 * Alert types (alertType string):
 *   "brute_force"        — BRUTE_FORCE_LIMIT+ consecutive failed attempts
 *   "suspicious_signin"  — match score too close to threshold (spoofing risk)
 *   "high_frequency"     — abnormal sign-in rate
 *   "unknown_user"       — unregistered biometric repeated attempts
 */

#include <Arduino.h>

// Forward declarations to avoid circular includes
class AWSIoTManager;
class FirebaseManager;

static constexpr uint32_t ALERT_COOLDOWN_MS = 30000UL;  // 30 s per alert type

struct AlertPayload {
    char     thingName[64];
    char     userId[32];
    char     alertType[32];
    char     detail[128];
    float    anomalyScore;
    uint32_t ts;
};

class AlertManager {
public:
    AlertManager(const char* thingName, AWSIoTManager* aws, FirebaseManager* fb);

    // Returns true if alert was dispatched (not suppressed by cooldown).
    // Publishes to MQTT and logs to Firebase.
    bool sendAnomaly(const char* userId, float anomalyScore,
                     const char* alertType, const char* detail = "");

    // Call from AWSIoTManager subscription callback when the AI agent ACKs
    void onAgentAck(const char* userId, const char* alertType);

    uint32_t lastAlertMs()        const { return _lastAlertMs; }
    const char* lastAlertType()   const { return _lastAlertType; }

private:
    const char*      _thingName;
    AWSIoTManager*   _aws;
    FirebaseManager* _fb;
    uint32_t         _lastAlertMs;
    char             _lastAlertType[32];

    // Serialise AlertPayload to JSON string
    String _format(const AlertPayload& p) const;
};
