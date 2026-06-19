#pragma once
/*
 * BiometricManager.h
 * Iris template enrollment, persistence, and 1:N matching.
 *
 * Each user can have up to BIO_MAX_TEMPLATES iris templates, stored as
 * raw float32[64] binary files in SPIFFS:
 *   /bio/{userId}_t{0..N-1}.bin   — template files
 *   /bio/users.json               — enrolled user registry
 *
 * Matching metric: normalised Euclidean (RMS) distance over the 64 features.
 * Lower score = better match. Authenticated when score < threshold.
 *
 * Enrollment notes:
 *   - Call enroll() with an IrisCapture produced by IrisCamera::captureAverage()
 *     for the most stable template.
 *   - Templates accumulate up to BIO_MAX_TEMPLATES; oldest is overwritten.
 *   - MatchResult.score from match() is the distance to the closest template
 *     across all users.
 */

#include <Arduino.h>
#include "IrisCamera.h"

static constexpr uint8_t BIO_MAX_USERS     = 16;
static constexpr uint8_t BIO_MAX_TEMPLATES = 5;
static constexpr float   BIO_MATCH_THRESH  = 0.30f;  // distance threshold

struct UserRecord {
    char    userId[32];
    char    name[64];
    uint8_t templateCount;
    bool    active;
};

struct MatchResult {
    bool    matched;
    bool    authorized;       // iris matched AND device is in user's allowedDevices
    char    userId[32];
    char    userName[64];
    float   score;            // RMS distance — lower = more similar
    uint8_t templateIdx;
    char    denyReason[32];   // "none" | "no_match" | "unauthorized_device"
};

class BiometricManager {
public:
    BiometricManager();

    // Mount SPIFFS and load all enrolled templates
    bool begin();

    // Enroll user: stores iris.features as a new template in SPIFFS.
    // If user already has BIO_MAX_TEMPLATES, the oldest is overwritten.
    bool enroll(const char* userId, const char* name, const IrisCapture& iris);

    // Find best-matching user; matched=false when best score ≥ threshold
    MatchResult match(const IrisCapture& iris,
                      float threshold = BIO_MATCH_THRESH) const;

    uint8_t           userCount() const { return _userCount; }
    const UserRecord& user(uint8_t idx) const { return _users[idx]; }
    bool              isEnrolled(const char* userId) const;

    bool removeUser(const char* userId);
    bool saveRegistry() const;

private:
    UserRecord _users[BIO_MAX_USERS];
    float      _templates[BIO_MAX_USERS][BIO_MAX_TEMPLATES][IRIS_FEAT_DIM];
    uint8_t    _templateCount[BIO_MAX_USERS];
    uint8_t    _userCount;

    int    _findUser(const char* userId) const;
    float  _rms(const float* a, const float* b) const;
    bool   _saveTemplate(uint8_t ui, uint8_t ti) const;
    bool   _loadTemplate(uint8_t ui, uint8_t ti);
    String _tPath(uint8_t ui, uint8_t ti) const;
    bool   _loadAll();
};
