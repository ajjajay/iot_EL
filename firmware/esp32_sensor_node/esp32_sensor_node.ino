/*
 * esp32_sensor_node.ino
 * ─────────────────────────────────────────────────────────────────────────────
 * Smart Iris Biometric Access Control System — ESP32-CAM Firmware
 *
 * Architecture:
 *   setup()  → INIT → CONNECTING → READY → MONITORING
 *
 *   MONITORING (idle)
 *     │ short button press         │ Firebase enroll command
 *     ▼                            ▼
 *   AUTHENTICATING              ENROLLING
 *     │ match         │ no match   │ done
 *     ▼               ▼            ▼
 *   AUTHENTICATED  REJECTED     MONITORING
 *     │ anomaly score high
 *     ▼
 *   ALERT  ──(AI agent notified)──► MONITORING
 *
 * Sign-in data flow:
 *   ESP32-CAM → IrisCamera → BiometricManager (match)
 *     ↓                                         ↓
 *   AnomalyDetector                        AWSIoTManager (MQTT telemetry)
 *     ↓ anomaly                                 ↓
 *   AlertManager ──► AWS IoT → Lambda → Bedrock Agent → User notification
 *     ↓
 *   FirebaseManager (history + alerts log)
 *
 * Required Arduino Libraries:
 *   - Firebase ESP Client (mobizt)
 *   - ArduinoJson (Benoit Blanchon)
 *   - PubSubClient (Nick O'Leary)
 *   - TensorFlowLite_ESP32 (for legacy env. ML — can be disabled)
 *   - ESP32 camera driver (bundled with ESP32 Arduino core ≥ 2.0.11)
 *
 * Board: AI Thinker ESP32-CAM, 240 MHz, 4 MB Flash + 4 MB PSRAM
 *        Partition: "Huge APP" (3 MB app, 1 MB SPIFFS)
 */

#include <WiFi.h>
#include "ConfigManager.h"
#include "StateManager.h"
#include "SensorManager.h"
#include "ActuatorController.h"
#include "FirebaseManager.h"
#include "MLInference.h"
#include "aws_certificates.h"
#include "AWSIoTManager.h"
#include "IrisCamera.h"
#include "BiometricManager.h"
#include "AnomalyDetector.h"
#include "AlertManager.h"

// ── Module instances ──────────────────────────────────────────────────────────
ConfigManager        config;
StateManager         fsm;
SensorManager*       sensors    = nullptr;
ActuatorController*  actuators  = nullptr;
FirebaseManager*     firebase   = nullptr;
AWSIoTManager*       awsIoT     = nullptr;
MLInference          ml;
IrisCamera*          camera     = nullptr;
BiometricManager*    biometric  = nullptr;
AnomalyDetector*     anomaly    = nullptr;
AlertManager*        alertMgr   = nullptr;

// ── Timing ────────────────────────────────────────────────────────────────────
unsigned long lastHeartbeatMs  = 0;
unsigned long lastCommandMs    = 0;
unsigned long lastEnvSensorMs  = 0;

// ── Button state ──────────────────────────────────────────────────────────────
unsigned long buttonPressedAt  = 0;
bool          buttonWasPressed = false;

// ── Pending enrollment ────────────────────────────────────────────────────────
char pendingEnrollUserId[32] = {};
char pendingEnrollName[64]   = {};
bool enrollPending           = false;

// ── WiFi helper ───────────────────────────────────────────────────────────────
static bool connectWifi(const char* ssid, const char* pass,
                         uint32_t timeoutMs = 15000) {
    Serial.printf("[WiFi] Connecting to '%s'...", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(300); Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected — IP: %s  RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }
    Serial.println("[WiFi] Connection failed");
    return false;
}

// ── Button helpers ────────────────────────────────────────────────────────────
static bool isButtonShortPress(uint8_t pin, uint16_t debounceMs) {
    bool down = digitalRead(pin) == LOW;
    unsigned long now = millis();

    if (down && !buttonWasPressed) {
        buttonWasPressed = true;
        buttonPressedAt  = now;
        return false;  // still pressing
    }
    if (!down && buttonWasPressed) {
        buttonWasPressed = false;
        uint32_t held = now - buttonPressedAt;
        if (held >= debounceMs && held < 3000) return true;  // short press
    }
    return false;
}

static bool isButtonLongPress(uint8_t pin, uint16_t longPressMs) {
    if (!buttonWasPressed) return false;
    return (millis() - buttonPressedAt) >= longPressMs;
}

// ── State handlers ────────────────────────────────────────────────────────────

static void handleEnrolling(const DeviceConfig& c) {
    Serial.printf("[ENROLL] Starting enrollment for user '%s' ('%s')\n",
                  pendingEnrollUserId, pendingEnrollName);
    actuators->setLedPattern(LedPattern::BLINK_FAST);

    IrisCapture iris = camera->captureAverage(c.irisEnrollFrames, 200);

    if (!iris.valid) {
        Serial.println("[ENROLL] Capture failed — aborting");
        actuators->setLedPattern(LedPattern::BLINK_SLOW);
        fsm.transition(DeviceState::MONITORING);
        enrollPending = false;
        return;
    }

    if (biometric->enroll(pendingEnrollUserId, pendingEnrollName, iris)) {
        Serial.printf("[ENROLL] Success: %d template(s) for '%s'\n",
                      biometric->user(0).templateCount, pendingEnrollUserId);
        firebase->pushEnrollment(pendingEnrollUserId, pendingEnrollName);
        // 2-second solid LED = enrolled OK
        actuators->setLedPattern(LedPattern::ON);
        delay(2000);
    } else {
        Serial.println("[ENROLL] Save failed");
    }

    actuators->setLedPattern(LedPattern::BLINK_SLOW);
    memset(pendingEnrollUserId, 0, sizeof(pendingEnrollUserId));
    memset(pendingEnrollName,   0, sizeof(pendingEnrollName));
    enrollPending = false;
    fsm.transition(DeviceState::MONITORING);
}

static void handleAuthenticating(const DeviceConfig& c) {
    actuators->setLedPattern(LedPattern::BLINK_FAST);

    IrisCapture iris = camera->capture();

    if (!iris.valid) {
        Serial.println("[AUTH] Camera capture failed");
        fsm.transition(DeviceState::REJECTED);
        return;
    }

    MatchResult result = biometric->match(iris, c.irisMatchThreshold);

    // Get ambient environment as sign-in context
    SensorReading env = sensors->readNow();

    if (result.matched) {
        fsm.transition(DeviceState::AUTHENTICATED);
        actuators->setRelay(RelayState::ON);     // unlock door
        actuators->setLedPattern(LedPattern::ON);

        // Anomaly scoring
        float anomScore = anomaly->record(result, true);

        // Always log sign-in to Firebase
        firebase->pushSignIn(result.userId, result.userName,
                             result.score, true, anomScore);

        // Always publish telemetry to AWS
        if (awsIoT && awsIoT->isConnected()) {
            awsIoT->publishBiometricEvent(result.userId, result.score, true);
        }

        // Raise alert if anomalous
        if (anomScore >= c.anomalyScoreThreshold) {
            Serial.printf("[AUTH] Anomaly detected (score=%.3f) — alerting\n",
                          anomScore);
            const char* alertType =
                anomaly->consecutiveFailures() >= BRUTE_FORCE_LIMIT
                    ? "brute_force"
                    : "suspicious_signin";
            alertMgr->sendAnomaly(result.userId, anomScore, alertType);
            // Transition to ALERT only after AUTHENTICATED display period ends
            // (handled in main loop timeout check)
        }

    } else {
        fsm.transition(DeviceState::REJECTED);
        actuators->setLedPattern(LedPattern::BLINK_FAST);

        float anomScore = anomaly->record(result, false);

        firebase->pushSignIn(
            result.userId[0] ? result.userId : "unknown",
            result.userName[0] ? result.userName : "Unknown",
            result.score, false, anomScore);

        if (awsIoT && awsIoT->isConnected()) {
            awsIoT->publishBiometricEvent(
                result.userId[0] ? result.userId : "unknown",
                result.score, false);
        }

        // Brute force check
        float bfScore = anomaly->bruteForceScore();
        if (bfScore >= c.anomalyScoreThreshold) {
            alertMgr->sendAnomaly("unknown", bfScore, "brute_force",
                                  "Repeated failed iris attempts");
        }
    }

    // Log environmental context alongside sign-in
    if (env.valid && awsIoT && awsIoT->isConnected()) {
        MLResult envMl = ml.infer(env.temperatureC, env.humidityPct, env.lightNorm);
        awsIoT->publishReading(env, envMl, fsm.current());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Iris Biometric Access Control — Boot ===");

    config.load();
    const DeviceConfig& c = config.cfg();

    // Instantiate hardware modules
    sensors   = new SensorManager(c.dhtPin, c.dhtType, c.ldrPin);
    actuators = new ActuatorController(c.relayPin, c.ledPin);
    firebase  = new FirebaseManager(c.firebaseApiKey, c.firebaseDatabaseUrl,
                                     c.firebaseUserEmail, c.firebaseUserPassword,
                                     c.deviceId);
    camera    = new IrisCamera();  // AI Thinker default pins
    biometric = new BiometricManager();
    anomaly   = new AnomalyDetector(c.irisMatchThreshold);

    // Button pin
    pinMode(c.authButtonPin, INPUT_PULLUP);

    sensors->begin();
    actuators->begin();
    actuators->setLedPattern(LedPattern::BLINK_FAST);  // fast = booting

    // ML (environmental — optional, used for env context alongside sign-ins)
    if (!ml.begin()) {
        Serial.println("[BOOT] Env ML init failed — env inference disabled");
    }

    // Camera
    if (!camera->begin()) {
        Serial.println("[BOOT] Camera init failed — HALTING");
        fsm.transition(DeviceState::ERROR);
        actuators->setLedPattern(LedPattern::BLINK_SOS);
        while (true) { actuators->tick(); delay(10); }
    }

    // Biometric templates (from SPIFFS)
    biometric->begin();

    fsm.transition(DeviceState::CONNECTING);
    bool wifiOk = connectWifi(c.wifiSsid, c.wifiPassword);
    bool fbOk   = false;
    if (wifiOk) {
        fbOk = firebase->begin();
    }

    if (!wifiOk || !fbOk) {
        Serial.println("[BOOT] Offline mode — Firebase pushes will be queued");
    }

    // AWS IoT Core (optional — enabled by config)
    if (c.awsEnabled && wifiOk) {
        awsIoT = new AWSIoTManager(c.awsEndpoint, c.awsThingName,
                                   AWS_ROOT_CA, AWS_DEVICE_CERT, AWS_PRIVATE_KEY);
        if (!awsIoT->begin()) {
            Serial.println("[BOOT] AWS IoT connect failed — retried each loop");
        }
    }

    // AlertManager wired to AWS + Firebase
    alertMgr = new AlertManager(c.awsThingName[0] ? c.awsThingName : c.deviceId,
                                 awsIoT, firebase);

    fsm.transition(DeviceState::READY);
    fsm.transition(DeviceState::MONITORING);
    actuators->setLedPattern(LedPattern::BLINK_SLOW);

    Serial.printf("[BOOT] Device '%s' ready — %d user(s) enrolled\n",
                  c.deviceId, biometric->userCount());
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    const DeviceConfig& c = config.cfg();
    DeviceState          state = fsm.current();

    // ── WiFi watchdog ─────────────────────────────────────────────────────────
    if (state == DeviceState::MONITORING || state == DeviceState::ALERT) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Lost — reconnecting");
            fsm.transition(DeviceState::CONNECTING);
            actuators->setLedPattern(LedPattern::BLINK_FAST);
            bool reconnected = connectWifi(c.wifiSsid, c.wifiPassword, 20000);
            if (reconnected) {
                firebase->flushQueue();
                fsm.transition(DeviceState::MONITORING);
                actuators->setLedPattern(LedPattern::BLINK_SLOW);
            }
            return;
        }
    }

    // ── MQTT keep-alive ───────────────────────────────────────────────────────
    if (awsIoT) awsIoT->loop();

    // ── Service LED and relay override expiry ─────────────────────────────────
    actuators->tick();

    // ── Agent ACK handling ────────────────────────────────────────────────────
    if (awsIoT) {
        char ackUser[32], ackType[32];
        if (awsIoT->getAgentAck(ackUser, ackType)) {
            alertMgr->onAgentAck(ackUser, ackType);
        }
    }

    // ── Heartbeat ─────────────────────────────────────────────────────────────
    unsigned long now = millis();
    if (now - lastHeartbeatMs >= c.heartbeatIntervalMs) {
        lastHeartbeatMs = now;
        firebase->sendHeartbeat(fsm.current());
        if (awsIoT && awsIoT->isConnected()) awsIoT->publishHeartbeat(fsm.current());
    }

    // ── Poll Firebase commands ────────────────────────────────────────────────
    if (now - lastCommandMs >= 5000) {
        lastCommandMs = now;

        // Relay override (legacy or remote unlock)
        RelayState remoteRelay;
        if (firebase->pollCommands(remoteRelay)) {
            actuators->setRelayOverride(remoteRelay, 300000);
        }

        // Enrollment command from dashboard
        if (!enrollPending &&
            firebase->pollEnrollCommand(pendingEnrollUserId, sizeof(pendingEnrollUserId),
                                         pendingEnrollName,   sizeof(pendingEnrollName))) {
            enrollPending = true;
        }
    }

    // ── AWS relay command ─────────────────────────────────────────────────────
    if (awsIoT) {
        RelayState awsRelay;
        if (awsIoT->getRelayCommand(awsRelay)) {
            actuators->setRelayOverride(awsRelay, 300000);
        }
    }

    // ── State machine ─────────────────────────────────────────────────────────
    state = fsm.current();

    // ── MONITORING: wait for trigger ──────────────────────────────────────────
    if (state == DeviceState::MONITORING) {

        // Enrollment trigger: Firebase command takes priority
        if (enrollPending) {
            fsm.transition(DeviceState::ENROLLING);
            return;
        }

        // Long press: enter enrollment mode (waits for Firebase command for user ID)
        if (isButtonLongPress(c.authButtonPin, c.buttonLongPressMs) && !enrollPending) {
            Serial.println("[BTN] Long press — waiting for Firebase enrollment command");
            // LED pattern to signal awaiting enrollment command
            actuators->setLedPattern(LedPattern::BLINK_FAST);
            delay(500);
            actuators->setLedPattern(LedPattern::BLINK_SLOW);
            buttonWasPressed = false;  // reset so short-press can still work
            return;
        }

        // Short press: authenticate
        if (isButtonShortPress(c.authButtonPin, c.buttonDebounceMs)) {
            Serial.println("[BTN] Short press — starting authentication");
            fsm.transition(DeviceState::AUTHENTICATING);
            return;
        }
    }

    // ── ENROLLING ─────────────────────────────────────────────────────────────
    if (state == DeviceState::ENROLLING) {
        handleEnrolling(c);
        return;
    }

    // ── AUTHENTICATING ────────────────────────────────────────────────────────
    if (state == DeviceState::AUTHENTICATING) {
        handleAuthenticating(c);
        return;
    }

    // ── AUTHENTICATED / REJECTED: display timeout ─────────────────────────────
    if (state == DeviceState::AUTHENTICATED || state == DeviceState::REJECTED) {
        if (fsm.timeInState() >= c.authDisplayMs) {
            actuators->setRelay(RelayState::OFF);   // re-lock
            actuators->setLedPattern(LedPattern::BLINK_SLOW);

            // If anomalous, transition to ALERT for an extended notification period
            // (anomaly was already sent by handleAuthenticating; here we just track state)
            // Simplified: if the last record had high anomaly score, go ALERT
            // We check via AnomalyDetector — if consecutiveFailures is 0 and we had
            // a suspicious_signin, a record was already pushed; just go back to MONITORING
            fsm.transition(DeviceState::MONITORING);
        }
        return;
    }

    // ── ALERT: anomaly display period ────────────────────────────────────────
    if (state == DeviceState::ALERT) {
        if (fsm.timeInState() >= 10000) {   // 10 s visual alert
            actuators->setLedPattern(LedPattern::BLINK_SLOW);
            fsm.transition(DeviceState::MONITORING);
        }
        return;
    }

    // ── ERROR: restart after 10 s ────────────────────────────────────────────
    if (state == DeviceState::ERROR && fsm.timeInState() > 10000) {
        Serial.println("[MAIN] Rebooting after ERROR timeout");
        ESP.restart();
    }

    // ── Background: ambient env sensor (every sensorIntervalMs) ──────────────
    if (now - lastEnvSensorMs >= c.sensorIntervalMs &&
        state == DeviceState::MONITORING) {
        lastEnvSensorMs = now;
        SensorReading env = sensors->readNow();
        if (env.valid && awsIoT && awsIoT->isConnected()) {
            MLResult envMl = ml.infer(env.temperatureC, env.humidityPct, env.lightNorm);
            awsIoT->publishReading(env, envMl, fsm.current());
            firebase->pushReading(env, envMl, fsm.current());
        }
    }

    delay(10);
}
