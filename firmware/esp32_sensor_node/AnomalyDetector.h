#pragma once
/*
 * AnomalyDetector.h
 * Lightweight sliding-window anomaly detector for iris sign-in events.
 *
 * Computes a composite anomaly score (0.0 = normal, 1.0 = highly anomalous)
 * from three on-device signals:
 *
 *   1. Failure rate   — rapid consecutive failures → brute-force attack
 *   2. Score proximity— match score near the threshold → possible spoofing
 *   3. Frequency spike— more sign-ins than normal in the last 60 s
 *
 * Cloud-side anomaly detection (time-of-day patterns, historical behaviour)
 * is performed by the AWS Lambda function that consumes the sign-in events
 * published via AWSIoTManager::publishBiometricEvent().
 *
 * Usage:
 *   float score = detector.record(matchResult, success);
 *   if (score > cfg.anomalyScoreThreshold) { ... }
 */

#include <Arduino.h>
#include "BiometricManager.h"

static constexpr uint8_t ANOMALY_WINDOW    = 20;   // event ring-buffer depth
static constexpr uint8_t BRUTE_FORCE_LIMIT = 3;    // failures before max score

struct SignInEvent {
    char     userId[32];
    float    matchScore;
    bool     success;
    uint32_t timestampMs;
    bool     valid;
};

class AnomalyDetector {
public:
    explicit AnomalyDetector(float matchThreshold = BIO_MATCH_THRESH);

    // Record sign-in event; returns composite anomaly score (0.0–1.0).
    // Must be called for every authentication attempt (success or failure).
    float record(const MatchResult& result, bool success);

    // Brute-force-only check without recording (called on failed pre-match)
    float bruteForceScore() const;

    uint8_t consecutiveFailures() const { return _consecutiveFails; }

    // Expose event window so main loop can include it in Firebase/AWS uploads
    const SignInEvent& event(uint8_t idx) const { return _window[idx % ANOMALY_WINDOW]; }
    uint8_t            eventCount()        const { return _count; }

private:
    SignInEvent _window[ANOMALY_WINDOW];
    uint8_t     _head;
    uint8_t     _count;
    uint8_t     _consecutiveFails;
    float       _matchThreshold;

    float _scoreFailureRate()                const;
    float _scoreMatchProximity(float score)  const;
    float _scoreFrequency(uint32_t nowMs)    const;
};
