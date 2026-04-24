#pragma once
/*
 * SensorManager.h
 * Abstracts DHT22 (temperature + humidity) and LDR (light level) reads.
 *
 * Features:
 *   - Exponential moving average (EMA) smoothing to reduce noise
 *   - Read validation (NaN / out-of-range rejection)
 *   - Retry logic with configurable attempts
 *   - Mock mode: returns simulated data when SENSOR_MOCK is defined
 *     (useful for CI tests or bench development without hardware)
 */

#include <Arduino.h>
#include <DHT.h>

struct SensorReading {
    float    temperatureC;
    float    humidityPct;
    uint16_t lightRaw;       // 0–4095 (12-bit ADC)
    float    lightNorm;      // 0.0–1.0 normalised
    bool     valid;          // false if all retries failed
    uint32_t timestampMs;    // millis() at read time
};

class SensorManager {
public:
    // emaAlpha: EMA smoothing factor 0.0 (heavy smooth) – 1.0 (no smooth)
    SensorManager(uint8_t dhtPin, uint8_t dhtType, uint8_t ldrPin,
                  float emaAlpha = 0.3f);

    void begin();

    // Returns the latest reading; re-reads if intervalMs has elapsed
    SensorReading read(uint32_t intervalMs = 0);

    // Force a fresh read regardless of interval
    SensorReading readNow();

    // Last successful reading (never blocks)
    const SensorReading& last() const { return _last; }

    // Consecutive failure count since last success
    uint8_t failCount() const { return _failCount; }

private:
    DHT      _dht;
    uint8_t  _ldrPin;
    float    _emaAlpha;

    SensorReading _last;
    uint8_t       _failCount;
    unsigned long _lastReadMs;

    static constexpr uint8_t  MAX_RETRIES    = 3;
    static constexpr uint16_t RETRY_DELAY_MS = 100;

    // ADC smoothing: keep running sum for EMA
    float _emaTemp;
    float _emaHum;
    float _emaLight;
    bool  _emaInit;

    SensorReading _doRead();
    float _applyEma(float& ema, float newVal) const;
};
