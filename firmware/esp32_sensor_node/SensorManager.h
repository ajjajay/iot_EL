#pragma once
/*
 * SensorManager.h
 * Reads DHT11 (temperature + humidity), MQ-2 smoke sensor (analog),
 * and HC-SR04 ultrasonic distance sensor.
 *
 * EMA smoothing applied to DHT and smoke readings.
 * Ultrasonic distance is raw (pulseIn-based, blocking but <30 ms).
 */

#include <Arduino.h>
#include <DHT.h>

struct SensorReading {
    float    temperatureC;
    float    humidityPct;
    uint16_t smokeRaw;     // 0–4095 (12-bit ADC)
    float    smokePct;     // 0.0–100.0
    float    distanceCm;   // -1.0 = no object detected within range
    bool     valid;
    uint32_t timestampMs;
};

class SensorManager {
public:
    // smokePin: analog ADC pin for MQ-2
    // trigPin/echoPin: HC-SR04 digital pins
    SensorManager(uint8_t dhtPin, uint8_t dhtType,
                  uint8_t smokePin, uint8_t trigPin, uint8_t echoPin,
                  float emaAlpha = 0.3f);

    void begin();

    // Re-reads if intervalMs elapsed; returns last if not
    SensorReading read(uint32_t intervalMs = 0);

    // Force a fresh read
    SensorReading readNow();

    const SensorReading& last() const { return _last; }
    uint8_t failCount()        const { return _failCount; }

private:
    DHT     _dht;
    uint8_t _smokePin;
    uint8_t _trigPin;
    uint8_t _echoPin;
    float   _emaAlpha;

    SensorReading _last;
    uint8_t       _failCount;
    unsigned long _lastReadMs;

    float _emaTemp;
    float _emaHum;
    float _emaSmoke;
    bool  _emaInit;

    static constexpr uint8_t  MAX_RETRIES    = 3;
    static constexpr uint16_t RETRY_DELAY_MS = 100;

    SensorReading _doRead();
    float         _applyEma(float& ema, float newVal) const;
    float         _readDistance();
};
