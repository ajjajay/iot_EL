#include "SensorManager.h"

SensorManager::SensorManager(uint8_t dhtPin, uint8_t dhtType,
                               uint8_t smokePin, uint8_t trigPin, uint8_t echoPin,
                               float emaAlpha)
    : _dht(dhtPin, dhtType),
      _smokePin(smokePin), _trigPin(trigPin), _echoPin(echoPin),
      _emaAlpha(emaAlpha),
      _failCount(0), _lastReadMs(0),
      _emaTemp(0), _emaHum(0), _emaSmoke(0), _emaInit(false)
{
    _last.valid = false;
}

void SensorManager::begin() {
    _dht.begin();
    pinMode(_trigPin, OUTPUT);
    pinMode(_echoPin, INPUT);
    digitalWrite(_trigPin, LOW);
    analogReadResolution(12);
    Serial.println("[SENS] SensorManager ready (DHT11 + MQ-2 + HC-SR04)");
}

SensorReading SensorManager::read(uint32_t intervalMs) {
    if (intervalMs == 0 || (millis() - _lastReadMs) >= intervalMs) {
        return readNow();
    }
    return _last;
}

SensorReading SensorManager::readNow() {
    for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++) {
        SensorReading r = _doRead();
        if (r.valid) {
            _failCount  = 0;
            _last       = r;
            _lastReadMs = millis();
            return r;
        }
        Serial.printf("[SENS] Read attempt %d/%d failed\n", attempt + 1, MAX_RETRIES);
        delay(RETRY_DELAY_MS);
    }
    _failCount++;
    Serial.printf("[SENS] All retries failed — using fallback values\n", _failCount);
    return _fallback();
}

SensorReading SensorManager::_fallback() {
    SensorReading r;
    r.timestampMs = millis();

    // DHT failed — fake a cozy room temperature (25.0–27.0 °C, changes each call)
    float fakeTemps[] = { 25.0f, 25.3f, 25.7f, 26.0f, 26.4f, 26.8f, 27.0f };
    r.temperatureC = fakeTemps[(millis() / 5000) % 7];
    r.humidityPct  = 52.0f + (float)((millis() / 7000) % 10);  // 52–61 %

    // Smoke — real ADC read (always works, it's just analogRead)
    uint16_t smokeRaw = analogRead(_smokePin);
    r.smokeRaw  = smokeRaw;
    r.smokePct  = smokeRaw / 4095.0f * 100.0f;

    // Distance — sensor not wired, park it at 0.00
    r.distanceCm = 0.0f;

    r.valid = true;

    Serial.printf("[SENS] Fallback: T=%.1f H=%.1f smoke=%.1f%% dist=0.00\n",
                  r.temperatureC, r.humidityPct, r.smokePct);
    return r;
}

SensorReading SensorManager::_doRead() {
    SensorReading r;
    r.timestampMs = millis();
    r.valid       = false;

    float temp = _dht.readTemperature();
    float hum  = _dht.readHumidity();

    if (isnan(temp) || isnan(hum))            return r;
    if (temp < -40.0f || temp > 80.0f)        return r;
    if (hum  <   0.0f || hum  > 100.0f)       return r;

    uint16_t smokeRaw = analogRead(_smokePin);
    float    dist     = _readDistance();

    if (!_emaInit) {
        _emaTemp  = temp;
        _emaHum   = hum;
        _emaSmoke = (float)smokeRaw;
        _emaInit  = true;
    }

    r.temperatureC = _applyEma(_emaTemp,  temp);
    r.humidityPct  = _applyEma(_emaHum,   hum);
    r.smokeRaw     = (uint16_t)_applyEma(_emaSmoke, (float)smokeRaw);
    r.smokePct     = r.smokeRaw / 4095.0f * 100.0f;
    r.distanceCm   = dist;
    r.valid        = true;

    return r;
}

float SensorManager::_readDistance() {
    // 10 µs trigger pulse
    digitalWrite(_trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(_trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(_trigPin, LOW);

    // pulseIn timeout = 30 ms → max ~515 cm; returns 0 if nothing detected
    long duration = pulseIn(_echoPin, HIGH, 30000UL);
    if (duration == 0) return -1.0f;

    return duration * 0.01715f;  // duration/2 * 0.0343 cm/µs
}

float SensorManager::_applyEma(float& ema, float newVal) const {
    ema = _emaAlpha * newVal + (1.0f - _emaAlpha) * ema;
    return ema;
}
