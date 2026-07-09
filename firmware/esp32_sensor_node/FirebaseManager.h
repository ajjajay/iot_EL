#pragma once
/*
 * FirebaseManager.h
 * Wraps Firebase ESP Client library for:
 *   - Authentication (email/password)
 *   - Pushing sensor readings to Realtime Database
 *   - Receiving actuator commands (polling)
 *   - Offline queue: if WiFi is down, readings are buffered in a ring buffer
 *     and flushed once reconnected (prevents data loss)
 *   - Heartbeat: periodic timestamp update so the dashboard knows the device
 *     is alive
 */

#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "SensorManager.h"
#include "MLInference.h"
#include "StateManager.h"
#include "ActuatorController.h"

// Ring buffer capacity (readings) — each entry ≈ 60 bytes
static constexpr uint8_t OFFLINE_QUEUE_SIZE = 30;

struct QueuedReading {
    SensorReading sensor;
    MLResult      ml;
    DeviceState   state;
    bool          occupied;
};

class FirebaseManager {
public:
    FirebaseManager(const char* apiKey, const char* databaseUrl,
                    const char* userEmail, const char* userPassword,
                    const char* deviceId,
                    const char* storageBucket = "");

    // Call once in setup()
    bool begin();

    // Push latest sensor+ML data; queues locally if offline
    void pushReading(const SensorReading& s, const MLResult& ml,
                     DeviceState state);

    // Poll Firebase for actuator commands from dashboard; returns true if relay
    // override changed
    bool pollCommands(RelayState& outRelay);

    // Update device heartbeat + online status
    void sendHeartbeat(DeviceState state);

    // Flush offline queue — call when WiFi reconnects
    void flushQueue();

    // Update device online/offline presence
    void setOnline(bool online);

    // Upload a JPEG buffer to Firebase Storage; returns the remote path on success,
    // empty string on failure. The path is included in MQTT payloads for Lambda to use.
    String uploadJpegToStorage(const uint8_t* buf, size_t len, const String& remotePath);

    // ── Biometric logging ────────────────────────────────────────────────────

    // Log one sign-in attempt under /signins/{deviceId}/{pushId}
    // denyReason: "none" | "no_match" | "unauthorized_device"
    void pushSignIn(const char* userId, const char* userName,
                    float matchScore, bool success, float anomalyScore,
                    const char* denyReason = "none");

    // Check whether userId is authorised for this device.
    // Reads /users/{userId}/role and /users/{userId}/allowedDevices from Firebase.
    // Returns true if role=="admin" or this device's ID is in allowedDevices.
    // Falls back to true (allow) when offline so device stays operational.
    bool checkUserAuthorization(const char* userId);

    // Register a new enrolled user under /users/{userId}
    void pushEnrollment(const char* userId, const char* name);

    // Log anomaly alert under /alerts/{deviceId}/{pushId}
    void pushBiometricAlert(const char* userId, const char* alertType,
                            float anomalyScore);

    // Poll /devices/{deviceId}/commands/enroll for dashboard-initiated enrollment.
    // Returns true and fills userId/name when a pending command is found; clears it.
    bool pollEnrollCommand(char* outUserId, uint8_t userIdLen,
                           char* outName,   uint8_t nameLen);

    // Poll /signins/{deviceId} for the latest sign-in pushed by the dashboard.
    // Returns true if a sign-in newer than lastSeenTs is found.
    // Fills outName, outSuccess, outTs.
    bool pollLatestSignIn(double lastSeenTs, char* outName, uint8_t nameLen,
                          bool& outSuccess, double& outTs);

    // Write /devices/{deviceId}/commands/qrRequest = {pending:true}
    // Called when the user triple-presses the QR button on the keypad.
    void requestQrCode();

    // Set /devices/{deviceId}/commands/qrUnlocked = true/false
    void setQrUnlocked(bool unlocked);

    bool isConnected() const { return _connected; }

private:
    FirebaseData   _fbData;        // writes: pushReading, sendHeartbeat, setOnline
    FirebaseData   _fbDataRead;    // reads:  pollCommands, pollEnrollCommand
    FirebaseData   _fbDataSignIn;  // reads:  pollLatestSignIn (isolated)
    FirebaseAuth   _fbAuth;
    FirebaseConfig _fbConfig;

    const char* _deviceId;
    char        _storageBucket[80];
    bool        _connected;
    bool        _authenticated;

    QueuedReading _queue[OFFLINE_QUEUE_SIZE];
    uint8_t       _queueHead;
    uint8_t       _queueCount;

    void _enqueue(const SensorReading& s, const MLResult& ml, DeviceState st);
    bool _pushOne(const SensorReading& s, const MLResult& ml, DeviceState st);
    String _devicePath() const;   // /devices/{deviceId}
    String _readingsPath() const; // /readings/{deviceId}
};
