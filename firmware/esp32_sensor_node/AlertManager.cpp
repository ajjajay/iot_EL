#include "AlertManager.h"
#include "AWSIoTManager.h"
#include "FirebaseManager.h"
#include <ArduinoJson.h>

AlertManager::AlertManager(const char* thingName, AWSIoTManager* aws, FirebaseManager* fb)
    : _thingName(thingName), _aws(aws), _fb(fb), _lastAlertMs(0)
{
    _lastAlertType[0] = '\0';
}

bool AlertManager::sendAnomaly(const char* userId, float anomalyScore,
                                const char* alertType, const char* detail) {
    uint32_t now = millis();

    // Suppress duplicate alert types within cooldown window
    if (strcmp(_lastAlertType, alertType) == 0 &&
        (now - _lastAlertMs) < ALERT_COOLDOWN_MS) {
        Serial.printf("[ALERT] Cooldown active for '%s' — suppressed\n", alertType);
        return false;
    }

    AlertPayload p;
    strlcpy(p.thingName,  _thingName,  sizeof(p.thingName));
    strlcpy(p.userId,     userId,      sizeof(p.userId));
    strlcpy(p.alertType,  alertType,   sizeof(p.alertType));
    strlcpy(p.detail,     detail ? detail : "", sizeof(p.detail));
    p.anomalyScore = anomalyScore;
    p.ts           = now;

    String json = _format(p);

    bool dispatched = false;

    // Publish to AWS IoT → triggers Lambda → Bedrock Agent → user notification
    if (_aws) {
        dispatched = _aws->publishAlertJson(json);
    }

    // Log to Firebase for history and dashboard visibility
    if (_fb) {
        _fb->pushBiometricAlert(userId, alertType, anomalyScore);
    }

    if (dispatched || _fb) {
        _lastAlertMs = now;
        strlcpy(_lastAlertType, alertType, sizeof(_lastAlertType));
        Serial.printf("[ALERT] Sent: type=%s user=%s score=%.3f\n",
                      alertType, userId, anomalyScore);
    }

    return dispatched;
}

void AlertManager::onAgentAck(const char* userId, const char* alertType) {
    Serial.printf("[ALERT] Agent ACK: type=%s user=%s — notification delivered\n",
                  alertType, userId);
    // Extend cooldown to prevent re-alerting after confirmed delivery
    _lastAlertMs = millis();
}

String AlertManager::_format(const AlertPayload& p) const {
    StaticJsonDocument<384> doc;
    doc["deviceId"]    = p.thingName;
    doc["userId"]      = p.userId;
    doc["alertType"]   = p.alertType;
    doc["anomalyScore"] = p.anomalyScore;
    doc["detail"]      = p.detail;
    doc["ts"]          = p.ts;

    String out;
    serializeJson(doc, out);
    return out;
}
