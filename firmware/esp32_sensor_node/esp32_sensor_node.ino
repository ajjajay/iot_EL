/*
 * esp32_sensor_node.ino
 * ─────────────────────────────────────────────────────────────────────────────
 * Iris Biometric Access Control — ESP32 Firmware
 *
 * Hardware (current build):
 *   DHT11        — temperature + humidity  (pin 4)
 *   MQ-2         — smoke sensor            (pin 32, analog)
 *   HC-SR04      — ultrasonic distance     (TRIG=5, ECHO=18)
 *   16×2 I2C LCD — status display          (SDA=21, SCL=22, addr=0x27)
 *   3×4 Keypad   — PIN authentication      (ROW1-4: 13,12,14,27  COL1-3: 26,25,33)
 *
 * Authentication:
 *   Local  — keypad PIN (default "1234", override in config.json "accessPin")
 *   Remote — laptop webcam via web dashboard (signs in + sends Firebase relay cmd)
 *
 * Data flow:
 *   ESP32 → Firebase RTDB → Dashboard (live sensor charts, sign-in log, alerts)
 *   ESP32 → AWS IoT Core  → Lambda → Bedrock Agent → SNS notification
 *
 * Required Arduino Libraries:
 *   DHT sensor library (Adafruit), Firebase ESP Client (mobizt),
 *   ArduinoJson (≥6), PubSubClient, LiquidCrystal I2C, Keypad,
 *   TensorFlowLite_ESP32 (optional — env risk score)
 */

#define ML_DISABLED

#include <WiFi.h>
#include <time.h>
#include "ConfigManager.h"
#include "StateManager.h"
#include "SensorManager.h"
#include "ActuatorController.h"   // stub — keeps RelayState/LedPattern enums
#include "FirebaseManager.h"
#include "MLInference.h"
#include "aws_certificates.h"
#include "AWSIoTManager.h"
#include "AlertManager.h"
#include "LCDManager.h"
#include "KeypadManager.h"

// Camera / biometric modules — only compiled and used when cameraEnabled = true
#include "IrisCamera.h"
#include "BiometricManager.h"
#include "AnomalyDetector.h"

// ── Module instances ──────────────────────────────────────────────────────────
ConfigManager         config;
StateManager          fsm;
SensorManager*        sensors   = nullptr;
FirebaseManager*      firebase  = nullptr;
AWSIoTManager*        awsIoT    = nullptr;
MLInference           ml;
LCDManager*           lcd       = nullptr;
KeypadManager*        keypad    = nullptr;
AlertManager*         alertMgr  = nullptr;
AnomalyDetector*      anomaly   = nullptr;

// Camera / biometric (null when cameraEnabled = false)
IrisCamera*           camera    = nullptr;
BiometricManager*     biometric = nullptr;

// Needed by handleEnrolling / handleAuthenticating (iris path) — stub, does nothing
ActuatorController    actuators;

// ── Timing ────────────────────────────────────────────────────────────────────
unsigned long lastHeartbeatMs  = 0;
unsigned long lastCommandMs    = 0;
unsigned long lastEnvSensorMs  = 0;

// ── PIN auth state ────────────────────────────────────────────────────────────
char          pinBuf[8]        = {};
uint8_t       pinLen           = 0;
bool          pinWaiting       = false;
unsigned long pinAuthStartMs   = 0;

// ── Enrollment pending (from Firebase dashboard command) ──────────────────────
char pendingEnrollUserId[32] = {};
char pendingEnrollName[64]   = {};
bool enrollPending           = false;

// ── Latest sensor reading + ML result (kept for LCD cycling) ─────────────────
SensorReading lastEnvReading;
MLResult      lastMlResult;

// ── Dashboard sign-in tracking ────────────────────────────────────────────────
double        lastSignInTs      = 0;
unsigned long lastSignInPollMs  = 0;
unsigned long signInDisplayUntil = 0;  // millis() until which to hold LCD message

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

// ── Iris enrollment handler (only when cameraEnabled = true) ──────────────────
static void handleEnrolling(const DeviceConfig& c) {
    Serial.printf("[ENROLL] Starting enrollment for user '%s' ('%s')\n",
                  pendingEnrollUserId, pendingEnrollName);
    lcd->showMessage("Enrolling...", pendingEnrollName);

    // 1. Capture JPEG for Rekognition (single frame before feature averaging)
    String storagePath = "";
    JpegCapture jpeg = camera->captureJpeg();
    if (jpeg.valid) {
        String path = String("enrollments/") + c.deviceId + "/" + pendingEnrollUserId + ".jpg";
        storagePath = firebase->uploadJpegToStorage(jpeg.buf, jpeg.len, path);
        IrisCamera::freeJpeg(jpeg);
    } else {
        Serial.println("[ENROLL] JPEG capture failed — Rekognition indexing skipped");
    }

    // 2. Capture averaged iris features for local SPIFFS matching
    IrisCapture iris = camera->captureAverage(c.irisEnrollFrames, 200);

    if (!iris.valid) {
        Serial.println("[ENROLL] Feature capture failed — aborting");
        lcd->showMessage("Enroll failed", "No capture");
        fsm.transition(DeviceState::MONITORING);
        enrollPending = false;
        return;
    }

    if (biometric->enroll(pendingEnrollUserId, pendingEnrollName, iris)) {
        Serial.printf("[ENROLL] Success: '%s'\n", pendingEnrollUserId);
        firebase->pushEnrollment(pendingEnrollUserId, pendingEnrollName);

        // 3. Publish enrollment event → IoT Rule → Lambda → Rekognition IndexFaces
        if (awsIoT && awsIoT->isConnected()) {
            awsIoT->publishEnrollmentEvent(pendingEnrollUserId, pendingEnrollName,
                                           storagePath.c_str());
        }

        lcd->showMessage("Enrolled!", pendingEnrollName);
        delay(2000);
    } else {
        Serial.println("[ENROLL] Save failed");
        lcd->showMessage("Enroll failed", "Save error");
        delay(1500);
    }

    memset(pendingEnrollUserId, 0, sizeof(pendingEnrollUserId));
    memset(pendingEnrollName,   0, sizeof(pendingEnrollName));
    enrollPending = false;
    fsm.transition(DeviceState::MONITORING);
}

// ── Iris authentication handler (only when cameraEnabled = true) ──────────────
static void handleIrisAuth(const DeviceConfig& c) {
    lcd->showMessage("Scanning iris...", "");

    // 1. Capture JPEG for Rekognition (upload before local matching)
    String storagePath = "";
    JpegCapture jpeg = camera->captureJpeg();
    if (jpeg.valid) {
        String path = String("signins/") + c.deviceId + "/" + String(millis()) + ".jpg";
        storagePath = firebase->uploadJpegToStorage(jpeg.buf, jpeg.len, path);
        IrisCamera::freeJpeg(jpeg);
    } else {
        Serial.println("[AUTH] JPEG capture failed — Rekognition will be skipped by Lambda");
    }

    // 2. Capture features for local matching
    IrisCapture iris = camera->capture();

    if (!iris.valid) {
        Serial.println("[AUTH] Camera capture failed");
        fsm.transition(DeviceState::REJECTED);
        lcd->showAuth(false, "No capture");
        return;
    }

    MatchResult result = biometric->match(iris, c.irisMatchThreshold);

    if (result.matched) {
        fsm.transition(DeviceState::AUTHENTICATED);
        float anomScore = anomaly->record(result, true);
        firebase->pushSignIn(result.userId, result.userName, result.score, true, anomScore);
        // Publish to AWS — Lambda runs Rekognition, anomaly check, logs, SNS if needed
        if (awsIoT && awsIoT->isConnected())
            awsIoT->publishBiometricEvent(result.userId, result.score, true,
                                          storagePath.c_str());
        if (anomScore >= c.anomalyScoreThreshold) {
            const char* t = anomaly->consecutiveFailures() >= 5 ? "brute_force" : "suspicious_signin";
            alertMgr->sendAnomaly(result.userId, anomScore, t);
        }
        lcd->showAuth(true, result.userName);
    } else {
        fsm.transition(DeviceState::REJECTED);
        float anomScore = anomaly->record(result, false);
        const char* uid  = result.userId[0]   ? result.userId   : "unknown";
        const char* unam = result.userName[0] ? result.userName : "Unknown";
        firebase->pushSignIn(uid, unam, result.score, false, anomScore);
        // Even on local rejection, publish to Lambda — Rekognition may still identify them
        if (awsIoT && awsIoT->isConnected())
            awsIoT->publishBiometricEvent(uid, result.score, false, storagePath.c_str());
        if (anomaly->bruteForceScore() >= c.anomalyScoreThreshold)
            alertMgr->sendAnomaly("unknown", anomaly->bruteForceScore(), "brute_force");
        lcd->showAuth(false, "No match");
    }
}

// ── PIN authentication handler (keypad, non-blocking) ─────────────────────────
static void handlePinAuth(const DeviceConfig& c) {
    // First entry — initialise
    if (!pinWaiting) {
        pinWaiting    = true;
        pinAuthStartMs = millis();
        pinLen        = 0;
        memset(pinBuf, 0, sizeof(pinBuf));
        lcd->showPinEntry("");
        return;
    }

    // Timeout after 30 s
    if (millis() - pinAuthStartMs > 30000) {
        pinWaiting = false;
        fsm.transition(DeviceState::REJECTED);
        lcd->showAuth(false, "Timeout");
        firebase->pushSignIn("pin_auth", "PIN Auth", 1.0f, false, 0.0f);
        return;
    }

    char key = keypad->getKey();
    if (key == '\0') return;

    if (key == '*') {
        // Clear
        pinLen = 0;
        memset(pinBuf, 0, sizeof(pinBuf));
        lcd->showPinEntry("");
    } else if (key == '#') {
        // Submit
        pinBuf[pinLen] = '\0';
        bool correct   = (strcmp(pinBuf, c.accessPin) == 0);
        pinWaiting     = false;

        if (correct) {
            fsm.transition(DeviceState::AUTHENTICATED);
            lcd->showAuth(true, "PIN Accepted");
            firebase->pushSignIn("pin_auth", "PIN Authenticated", 0.0f, true, 0.0f);
            if (awsIoT && awsIoT->isConnected())
                awsIoT->publishBiometricEvent("pin_auth", 0.0f, true);
        } else {
            fsm.transition(DeviceState::REJECTED);
            lcd->showAuth(false, "Wrong PIN");
            firebase->pushSignIn("pin_auth", "PIN Auth", 1.0f, false, 0.5f);
            if (awsIoT && awsIoT->isConnected())
                awsIoT->publishBiometricEvent("pin_auth", 1.0f, false);
        }
    } else if (pinLen < 7) {
        pinBuf[pinLen++] = key;
        char masked[8]   = {};
        for (uint8_t i = 0; i < pinLen; i++) masked[i] = '*';
        masked[pinLen] = '\0';
        lcd->showPinEntry(masked);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Iris Biometric Access Control — Boot ===");

    config.load();
    const DeviceConfig& c = config.cfg();

    // Instantiate modules
    sensors  = new SensorManager(c.dhtPin, c.dhtType,
                                  c.smokePin, c.ultrasonicTrig, c.ultrasonicEcho);
    firebase = new FirebaseManager(c.firebaseApiKey, c.firebaseDatabaseUrl,
                                    c.firebaseUserEmail, c.firebaseUserPassword,
                                    c.deviceId, c.firebaseStorageBucket);
    lcd    = new LCDManager();
    keypad = new KeypadManager();

    lcd->begin();
    lcd->showMessage("Booting...", c.deviceId);

    sensors->begin();
    keypad->begin();

    // ML (env risk score — uses smokePct as 3rd input instead of lightNorm)
    if (!ml.begin()) {
        Serial.println("[BOOT] Env ML init failed — env inference disabled");
    }

    // Camera + biometrics (skip when cameraEnabled = false)
    if (c.cameraEnabled) {
        camera    = new IrisCamera();
        biometric = new BiometricManager();
        if (!camera->begin()) {
            Serial.println("[BOOT] Camera init failed — HALTING");
            fsm.transition(DeviceState::ERROR);
            lcd->showMessage("Camera FAIL", "Halting...");
            while (true) delay(10);
        }
        biometric->begin();
        anomaly = new AnomalyDetector(c.irisMatchThreshold);
        Serial.printf("[BOOT] Camera OK — %d user(s) enrolled in SPIFFS\n",
                      biometric->userCount());
    } else {
        Serial.println("[BOOT] Camera disabled — iris auth via web dashboard");
    }

    fsm.transition(DeviceState::CONNECTING);
    lcd->showMessage("Connecting...", "WiFi");
    bool wifiOk = connectWifi(c.wifiSsid, c.wifiPassword);

    if (wifiOk) {
        // Show IP on LCD so you can confirm connectivity
        lcd->showMessage("WiFi OK", WiFi.localIP().toString().c_str());
        delay(2000);

        // NTP time sync — needed for correct Firebase timestamps
        lcd->showMessage("Syncing NTP...", "pool.ntp.org");
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        uint32_t ntpStart = millis();
        while (time(nullptr) < 1000000000UL && millis() - ntpStart < 8000) delay(200);
        if (time(nullptr) > 1000000000UL) {
            Serial.printf("[NTP] Synced — Unix time: %lu\n", (unsigned long)time(nullptr));
            lcd->showMessage("NTP synced", "Time OK");
        } else {
            Serial.println("[NTP] Sync failed — timestamps will be approximate");
            lcd->showMessage("NTP failed", "Check internet");
        }
        delay(1000);
    }

    bool fbOk = false;
    if (wifiOk) {
        lcd->showMessage("Firebase", "Connecting...");
        fbOk = firebase->begin();
    }

    if (!wifiOk || !fbOk) {
        Serial.println("[BOOT] Offline mode — readings will be queued");
        lcd->showMessage("Offline mode", "Queue active");
        delay(1000);
    }

    // AWS IoT Core
    if (c.awsEnabled && wifiOk) {
        awsIoT = new AWSIoTManager(c.awsEndpoint, c.awsThingName,
                                   AWS_ROOT_CA, AWS_DEVICE_CERT, AWS_PRIVATE_KEY);
        if (!awsIoT->begin()) {
            Serial.println("[BOOT] AWS IoT connect failed — retried each loop");
        }
    }

    alertMgr = new AlertManager(c.awsThingName[0] ? c.awsThingName : c.deviceId,
                                 awsIoT, firebase);

    fsm.transition(DeviceState::READY);
    fsm.transition(DeviceState::MONITORING);

    // Initial sensor read for LCD
    lastEnvReading = sensors->readNow();

    Serial.printf("[BOOT] Device '%s' ready\n", c.deviceId);
    lcd->showMessage("Ready", c.deviceId);
    delay(1000);
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    const DeviceConfig& c    = config.cfg();
    DeviceState          state = fsm.current();

    // ── WiFi watchdog ─────────────────────────────────────────────────────────
    if (state == DeviceState::MONITORING || state == DeviceState::ALERT) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Lost — reconnecting");
            fsm.transition(DeviceState::CONNECTING);
            lcd->showMessage("WiFi lost...", "Reconnecting");
            bool ok = connectWifi(c.wifiSsid, c.wifiPassword, 20000);
            if (ok) {
                firebase->flushQueue();
                fsm.transition(DeviceState::MONITORING);
                lcd->showMessage("WiFi OK", c.deviceId);
            }
            return;
        }
    }

    // ── Firebase token refresh — must be called every loop iteration ─────────
    Firebase.ready();

    // ── MQTT keep-alive ───────────────────────────────────────────────────────
    if (awsIoT) awsIoT->loop();

    // ── Agent ACK handling ────────────────────────────────────────────────────
    if (awsIoT) {
        char ackUser[32], ackType[32];
        if (awsIoT->getAgentAck(ackUser, ackType))
            alertMgr->onAgentAck(ackUser, ackType);
    }

    // ── Heartbeat ─────────────────────────────────────────────────────────────
    unsigned long now = millis();
    if (now - lastHeartbeatMs >= c.heartbeatIntervalMs) {
        lastHeartbeatMs = now;
        firebase->sendHeartbeat(fsm.current());
        if (awsIoT && awsIoT->isConnected()) awsIoT->publishHeartbeat(fsm.current());
    }

    // ── Poll Firebase commands every 5 s ─────────────────────────────────────
    if (now - lastCommandMs >= 5000) {
        lastCommandMs = now;

        // Enrollment command from dashboard (only used when cameraEnabled = true)
        if (!enrollPending && c.cameraEnabled)
            firebase->pollEnrollCommand(pendingEnrollUserId, sizeof(pendingEnrollUserId),
                                         pendingEnrollName,   sizeof(pendingEnrollName));
        if (pendingEnrollUserId[0]) enrollPending = true;
    }

    // ── Refresh state after polling ───────────────────────────────────────────
    state = fsm.current();

    // ── MONITORING ────────────────────────────────────────────────────────────
    if (state == DeviceState::MONITORING) {

        // Poll dashboard sign-ins every 5 s — always runs, even while displaying
        if (now - lastSignInPollMs >= 5000) {
            lastSignInPollMs = now;
            char   siName[32];
            bool   siSuccess;
            double siTs;
            if (firebase->pollLatestSignIn(lastSignInTs, siName, sizeof(siName),
                                           siSuccess, siTs)) {
                lastSignInTs       = siTs;
                signInDisplayUntil = now + 20000;
                lcd->showAuth(siSuccess, siName);  // immediately overrides whatever is on LCD
            }
        }

        // Suppress sensor screen cycling while showing a sign-in result
        if (now < signInDisplayUntil) return;

        // Enrollment (iris path only)
        if (enrollPending && c.cameraEnabled) {
            fsm.transition(DeviceState::ENROLLING);
            return;
        }

        // Any keypad press → start PIN auth
        char key = keypad->getKey();
        if (key != '\0') {
            Serial.printf("[KPD] Key '%c' pressed — starting PIN auth\n", key);
            fsm.transition(DeviceState::AUTHENTICATING);
            return;
        }

        // Cycle LCD through sensor readings
        if (lastEnvReading.valid) {
            lcd->tickSensorScreens(lastEnvReading, lastMlResult.riskScore,
                                   lastMlResult.label, stateToString(fsm.current()));
        }
    }

    // ── ENROLLING (iris) ──────────────────────────────────────────────────────
    if (state == DeviceState::ENROLLING) {
        if (!c.cameraEnabled) { fsm.transition(DeviceState::MONITORING); return; }
        handleEnrolling(c);
        return;
    }

    // ── AUTHENTICATING ────────────────────────────────────────────────────────
    if (state == DeviceState::AUTHENTICATING) {
        if (c.cameraEnabled) {
            handleIrisAuth(c);
        } else {
            handlePinAuth(c);
        }
        return;
    }

    // ── AUTHENTICATED / REJECTED: display timeout ─────────────────────────────
    if (state == DeviceState::AUTHENTICATED || state == DeviceState::REJECTED) {
        if (fsm.timeInState() >= c.authDisplayMs) {
            fsm.transition(DeviceState::MONITORING);
        }
        return;
    }

    // ── ALERT: environmental anomaly ─────────────────────────────────────────
    if (state == DeviceState::ALERT) {
        if (fsm.timeInState() >= 10000) {
            fsm.transition(DeviceState::MONITORING);
        }
        return;
    }

    // ── ERROR: restart ────────────────────────────────────────────────────────
    if (state == DeviceState::ERROR && fsm.timeInState() > 10000) {
        Serial.println("[MAIN] Rebooting after ERROR timeout");
        ESP.restart();
    }

    // ── Background: ambient sensor read every sensorIntervalMs ───────────────
    if (now - lastEnvSensorMs >= c.sensorIntervalMs &&
        state == DeviceState::MONITORING) {
        lastEnvSensorMs = now;
        SensorReading env = sensors->readNow();
        if (env.valid) {
            lastEnvReading = env;
            MLResult envMl = ml.infer(env.temperatureC, env.humidityPct,
                                       env.smokePct / 100.0f);
            lastMlResult   = envMl;

            // ── Serial debug dump ─────────────────────────────────────────────
            Serial.println("┌─────────────────────────────────────────┐");
            Serial.printf( "│ State     : %-28s│\n", stateToString(fsm.current()));
            Serial.printf( "│ Temp      : %.2f °C                      │\n", env.temperatureC);
            Serial.printf( "│ Humidity  : %.2f %%                       │\n", env.humidityPct);
            Serial.printf( "│ Smoke raw : %u  (%.1f %%)                 │\n", env.smokeRaw, env.smokePct);
            Serial.printf( "│ Distance  : %.1f cm                       │\n", env.distanceCm);
            Serial.printf( "│ Risk score: %.3f  label=%d (%s)       │\n",
                           envMl.riskScore, envMl.label,
                           envMl.label == 0 ? "NORMAL " : envMl.label == 1 ? "WARNING" : "CRITICAL");
            Serial.printf( "│ p(norm)   : %.3f  p(warn): %.3f  p(crit): %.3f │\n",
                           envMl.pNormal, envMl.pWarning, envMl.pCritical);
            Serial.printf( "│ Unix time : %lu                           │\n", (unsigned long)time(nullptr));
            Serial.printf( "│ Heap free : %u bytes                      │\n", (uint32_t)ESP.getFreeHeap());
            Serial.println("└─────────────────────────────────────────┘");
            // ─────────────────────────────────────────────────────────────────

            firebase->pushReading(env, envMl, fsm.current());
            if (awsIoT && awsIoT->isConnected())
                awsIoT->publishReading(env, envMl, fsm.current());

            if (envMl.riskScore >= c.mlRiskThreshold)
                fsm.transition(DeviceState::ALERT);
        }
    }

    delay(10);
}
