#include "SensorManager.h"

SensorManager::SensorManager(uint8_t dhtPin, uint8_t dhtType, uint8_t ldrPin,
                               float emaAlpha)
    : _dht(dhtPin, dhtType),
      _ldrPin(ldrPin),
      _emaAlpha(emaAlpha),
      _failCount(0),
      _lastReadMs(0),
      _emaTemp(0), _emaHum(0), _emaLight(0), _emaInit(false)
{
    _last.valid = false;
}

void SensorManager::begin() {
    _dht.begin();
    analogReadResolution(12);  // ESP32 native 12-bit ADC
    Serial.println("[SENS] SensorManager ready");
}

SensorReading SensorManager::read(uint32_t intervalMs) {
    if (intervalMs == 0 || (millis() - _lastReadMs) >= intervalMs) {
        return readNow();
    }
    return _last;
}

SensorReading SensorManager::readNow() {
#ifdef SENSOR_MOCK
    // ── Mock mode: return simulated sinusoidal data ──────────────────────────
    float t = millis() / 1000.0f;
    SensorReading r;
    r.temperatureC = 25.0f + 10.0f * sinf(t / 30.0f);  // 15–35 °C wave
    r.humidityPct  = 55.0f + 20.0f * cosf(t / 45.0f);  // 35–75 %
    r.lightRaw     = (uint16_t)(2048 + 2000 * sinf(t / 20.0f));
    r.lightNorm    = r.lightRaw / 4095.0f;
    r.valid        = true;
    r.timestampMs  = millis();
    _last          = r;
    _failCount     = 0;
    _lastReadMs    = millis();
    return r;
#endif

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
    Serial.printf("[SENS] All retries failed (total failures: %d)\n", _failCount);
    // Return last valid reading with updated timestamp to indicate staleness
    _last.valid = false;
    return _last;
}

SensorReading SensorManager::_doRead() {
    SensorReading r;
    r.timestampMs = millis();
    r.valid       = false;

    float temp = _dht.readTemperature();
    float hum  = _dht.readHumidity();

    // DHT returns NaN on failure
    if (isnan(temp) || isnan(hum)) return r;

    // Plausibility check — reject physically impossible values
    if (temp < -40.0f || temp > 80.0f) return r;
    if (hum  <   0.0f || hum  > 100.0f) return r;

    uint16_t ldrRaw = analogRead(_ldrPin);

    // Initialise EMA on first valid read
    if (!_emaInit) {
        _emaTemp  = temp;
        _emaHum   = hum;
        _emaLight = (float)ldrRaw;
        _emaInit  = true;
    }

    r.temperatureC = _applyEma(_emaTemp,  temp);
    r.humidityPct  = _applyEma(_emaHum,   hum);
    r.lightRaw     = (uint16_t)_applyEma(_emaLight, (float)ldrRaw);
    r.lightNorm    = r.lightRaw / 4095.0f;
    r.valid        = true;

    return r;
}

// EMA: new_ema = alpha * new_sample + (1 - alpha) * old_ema
float SensorManager::_applyEma(float& ema, float newVal) const {
    ema = _emaAlpha * newVal + (1.0f - _emaAlpha) * ema;
    return ema;
}
