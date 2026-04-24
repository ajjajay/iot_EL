/*
 * esp32_sensor_node.ino
 * ─────────────────────────────────────────────────────────────────────────────
 * IoT Smart Monitoring System — ESP32 Firmware
 * GitHub: https://github.com/ajjajay/iot_EL
 *
 * Architecture:
 *   setup() → INIT → CONNECTING → READY → MONITORING ⇄ ALERT
 *                                                    ↓ (WiFi lost)
 *                                               CONNECTING
 *
 * Each loop() iteration:
 *   1. Run state machine tick
 *   2. Read sensors (if interval elapsed)
 *   3. Run ML inference
 *   4. Evaluate thresholds + ML result → decide state transition
 *   5. Control actuators
 *   6. Push to Firebase (or queue offline)
 *   7. Poll Firebase commands (manual overrides)
 *   8. Send heartbeat (if interval elapsed)
 *   9. Service LED blink
 *
 * Required Arduino Libraries (install via Library Manager):
 *   - DHT sensor library (Adafruit)
 *   - Adafruit Unified Sensor
 *   - Firebase ESP Client (mobizt)
 *   - ArduinoJson (Benoit Blanchon)
 *   - TensorFlowLite_ESP32
 *
 * Board: ESP32 Dev Module, 240 MHz, 4 MB Flash, Default partition scheme
 */

#include <WiFi.h>
#include "ConfigManager.h"
#include "StateManager.h"
#include "SensorManager.h"
#include "ActuatorController.h"
#include "FirebaseManager.h"
#include "MLInference.h"

// ── Module instances ──────────────────────────────────────────────────────────
ConfigManager    config;
StateManager     fsm;
SensorManager*   sensors  = nullptr;
ActuatorController* actuators = nullptr;
FirebaseManager* firebase = nullptr;
MLInference      ml;

// ── Timing ────────────────────────────────────────────────────────────────────
unsigned long lastSensorMs    = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastCommandMs   = 0;

// ── WiFi helpers ─────────────────────────────────────────────────────────────
static bool connectWifi(const char* ssid, const char* pass, uint32_t timeoutMs = 15000) {
    Serial.printf("[WiFi] Connecting to '%s'...", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(300);
        Serial.print(".");
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

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== IoT Smart Monitor — Boot ===");

    // ── INIT ─────────────────────────────────────────────────────────────────
    // fsm starts in INIT automatically
    config.load();

    const DeviceConfig& c = config.cfg();

    // Instantiate modules with loaded config
    sensors   = new SensorManager(c.dhtPin, c.dhtType, c.ldrPin);
    actuators = new ActuatorController(c.relayPin, c.ledPin);
    firebase  = new FirebaseManager(c.firebaseApiKey, c.firebaseDatabaseUrl,
                                     c.firebaseUserEmail, c.firebaseUserPassword,
                                     c.deviceId);
    sensors->begin();
    actuators->begin();
    actuators->setLedPattern(LedPattern::BLINK_FAST);  // fast blink during boot

    bool mlOk = ml.begin();
    if (!mlOk) {
        Serial.println("[BOOT] ML init failed — using threshold-only fallback");
    }

    // ── CONNECTING ───────────────────────────────────────────────────────────
    fsm.transition(DeviceState::CONNECTING);
    bool wifiOk = connectWifi(c.wifiSsid, c.wifiPassword);
    bool fbOk   = false;
    if (wifiOk) {
        fbOk = firebase->begin();
    }

    if (!wifiOk || !fbOk) {
        Serial.println("[BOOT] No connectivity — offline mode active");
        // Still move to READY; Firebase pushes will be queued locally
    }

    // ── READY ────────────────────────────────────────────────────────────────
    fsm.transition(DeviceState::READY);
    actuators->setLedPattern(LedPattern::BLINK_SLOW);  // slow blink = connected + ready
    Serial.printf("[BOOT] Device '%s' ready at %s\n", c.deviceId, c.location);

    // Immediately transition to MONITORING (READY is just a transient state)
    fsm.transition(DeviceState::MONITORING);
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    const DeviceConfig& c = config.cfg();
    DeviceState          state = fsm.current();

    // ── WiFi watchdog ─────────────────────────────────────────────────────────
    if (state == DeviceState::MONITORING || state == DeviceState::ALERT) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Connection lost — attempting reconnect");
            fsm.transition(DeviceState::CONNECTING);
            actuators->setLedPattern(LedPattern::BLINK_FAST);
            bool reconnected = connectWifi(c.wifiSsid, c.wifiPassword, 20000);
            if (reconnected) {
                firebase->flushQueue();
                fsm.transition(DeviceState::MONITORING);
                actuators->setLedPattern(LedPattern::BLINK_SLOW);
            } else {
                // Stay in CONNECTING until next loop iteration retries
            }
            return;  // re-enter loop after reconnect attempt
        }
    }

    // ── Sensor read ───────────────────────────────────────────────────────────
    unsigned long now = millis();
    if (now - lastSensorMs >= c.sensorIntervalMs) {
        lastSensorMs = now;

        SensorReading reading = sensors->readNow();
        if (!reading.valid) {
            Serial.println("[MAIN] Sensor read failed — skipping cycle");
            // If sensor fails 5+ times in a row, escalate to ERROR
            if (sensors->failCount() >= 5) {
                fsm.transition(DeviceState::ERROR);
                actuators->setLedPattern(LedPattern::BLINK_SOS);
            }
            actuators->tick();
            return;
        }

        // ── ML Inference ──────────────────────────────────────────────────────
        MLResult mlResult = ml.infer(reading.temperatureC,
                                      reading.humidityPct,
                                      reading.lightNorm);

        // Fallback if ML failed: derive risk from thresholds only
        if (!mlResult.valid) {
            mlResult.riskScore = 0.0f;
            if (reading.temperatureC >= c.tempWarningC) mlResult.riskScore = 0.5f;
            if (reading.temperatureC >= c.tempCriticalC) mlResult.riskScore = 1.0f;
            mlResult.valid = true;
        }

        // ── State transitions ─────────────────────────────────────────────────
        bool shouldAlert = mlResult.riskScore >= c.mlRiskThreshold;
        state = fsm.current();  // refresh after potential WiFi reconnect

        if (state == DeviceState::MONITORING && shouldAlert) {
            Serial.printf("[MAIN] ALERT triggered — riskScore=%.3f\n",
                          mlResult.riskScore);
            fsm.transition(DeviceState::ALERT);
            actuators->setLedPattern(LedPattern::BLINK_FAST);
            actuators->setRelay(RelayState::ON);   // engage fan/cooler

        } else if (state == DeviceState::ALERT && !shouldAlert) {
            Serial.printf("[MAIN] Alert cleared — riskScore=%.3f\n",
                          mlResult.riskScore);
            fsm.transition(DeviceState::MONITORING);
            actuators->setLedPattern(LedPattern::BLINK_SLOW);
            actuators->setRelay(RelayState::OFF);
        }

        // ── Firebase push ─────────────────────────────────────────────────────
        firebase->pushReading(reading, mlResult, fsm.current());

        // ── Dashboard command polling (every 5 s) ──────────────────────────────
        if (now - lastCommandMs >= 5000) {
            lastCommandMs = now;
            RelayState remoteRelay;
            if (firebase->pollCommands(remoteRelay)) {
                actuators->setRelayOverride(remoteRelay, 300000);  // 5 min override
            }
        }
    }

    // ── Heartbeat ─────────────────────────────────────────────────────────────
    if (now - lastHeartbeatMs >= c.heartbeatIntervalMs) {
        lastHeartbeatMs = now;
        firebase->sendHeartbeat(fsm.current());
    }

    // ── Service LED and override expiry ───────────────────────────────────────
    actuators->tick();

    // ── ERROR recovery ────────────────────────────────────────────────────────
    if (fsm.current() == DeviceState::ERROR && fsm.timeInState() > 10000) {
        Serial.println("[MAIN] Rebooting after ERROR state timeout");
        ESP.restart();
    }

    delay(10);  // yield to RTOS tasks (WiFi stack, etc.)
}
