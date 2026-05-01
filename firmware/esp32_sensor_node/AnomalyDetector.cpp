#include "AnomalyDetector.h"
#include <math.h>

AnomalyDetector::AnomalyDetector(float matchThreshold)
    : _head(0), _count(0), _consecutiveFails(0), _matchThreshold(matchThreshold)
{
    memset(_window, 0, sizeof(_window));
}

float AnomalyDetector::record(const MatchResult& result, bool success) {
    SignInEvent ev;
    strlcpy(ev.userId, result.userId[0] ? result.userId : "unknown",
            sizeof(ev.userId));
    ev.matchScore  = result.score;
    ev.success     = success;
    ev.timestampMs = millis();
    ev.valid       = true;

    if (success) {
        _consecutiveFails = 0;
    } else {
        if (_consecutiveFails < 255) _consecutiveFails++;
    }

    _window[_head] = ev;
    _head = (_head + 1) % ANOMALY_WINDOW;
    if (_count < ANOMALY_WINDOW) _count++;

    float fail  = _scoreFailureRate();
    float prox  = _scoreMatchProximity(result.score);
    float freq  = _scoreFrequency(ev.timestampMs);
    float score = 0.40f * fail + 0.35f * prox + 0.25f * freq;
    score = score < 0.0f ? 0.0f : (score > 1.0f ? 1.0f : score);

    Serial.printf("[ANOM] score=%.3f  fail=%.2f prox=%.2f freq=%.2f\n",
                  score, fail, prox, freq);
    return score;
}

float AnomalyDetector::bruteForceScore() const {
    return _scoreFailureRate();
}

// BRUTE_FORCE_LIMIT or more consecutive failures → score 1.0
float AnomalyDetector::_scoreFailureRate() const {
    if (_consecutiveFails == 0) return 0.0f;
    float s = (float)_consecutiveFails / BRUTE_FORCE_LIMIT;
    return s > 1.0f ? 1.0f : s;
}

// Match score in the "just barely passed" zone is suspicious
// Score ramps from 0 → 1 as distance approaches the threshold
float AnomalyDetector::_scoreMatchProximity(float score) const {
    float lo = _matchThreshold * 0.30f;  // very confident match — no suspicion
    float hi = _matchThreshold;          // at threshold — max suspicion
    if (score < lo) return 0.0f;
    if (score >= hi) return 0.0f;        // failed match — handled by failure rate
    float s = (score - lo) / (hi - lo);
    return s > 1.0f ? 1.0f : s;
}

// More than 5 events in the last 60 s → anomalous frequency
float AnomalyDetector::_scoreFrequency(uint32_t nowMs) const {
    if (_count == 0) return 0.0f;
    const uint32_t WINDOW_MS = 60000UL;
    const uint8_t  NORMAL_MAX = 5;
    uint8_t recent = 0;
    for (uint8_t i = 0; i < _count; i++) {
        const SignInEvent& e = _window[i];
        if (e.valid && (nowMs - e.timestampMs) < WINDOW_MS) recent++;
    }
    if (recent <= NORMAL_MAX) return 0.0f;
    float s = (float)(recent - NORMAL_MAX) / NORMAL_MAX;
    return s > 1.0f ? 1.0f : s;
}
