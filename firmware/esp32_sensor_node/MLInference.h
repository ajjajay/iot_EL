#pragma once
/*
 * MLInference.h
 * Runs TFLite Micro inference on-device to produce a risk score
 * from [temperature, humidity, smoke_level] inputs.
 *
 * Define ML_DISABLED before including this header (or in the .ino) to
 * strip all TFLite code and save ~600 KB of flash. infer() then returns
 * a threshold-based result instead.
 *
 * Risk score (0.0–1.0) = p_warning * 0.5 + p_critical * 1.0
 */

#include <Arduino.h>

struct MLResult {
    float pNormal;
    float pWarning;
    float pCritical;
    float riskScore;
    uint8_t label;
    bool    valid;
};

#ifndef ML_DISABLED

#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

class MLInference {
public:
    MLInference();
    ~MLInference();

    bool begin();
    MLResult infer(float tempC, float humPct, float lightNorm);

    static constexpr float TEMP_MIN  = -10.0f;
    static constexpr float TEMP_MAX  =  60.0f;
    static constexpr float HUM_MIN   =   0.0f;
    static constexpr float HUM_MAX   = 100.0f;

private:
    const tflite::Model*      _model;
    tflite::MicroInterpreter* _interpreter;
    TfLiteTensor*             _inputTensor;
    TfLiteTensor*             _outputTensor;

    static constexpr size_t TENSOR_ARENA_SIZE = 8 * 1024;
    uint8_t _tensorArena[TENSOR_ARENA_SIZE];
    bool _ready;

    float _normalise(float val, float minVal, float maxVal) const;
};

#else  // ML_DISABLED — stub; threshold-only risk scoring, zero TFLite flash cost

class MLInference {
public:
    MLInference() {}
    ~MLInference() {}

    bool begin() {
        Serial.println("[ML] TFLite disabled — using threshold rules");
        return false;
    }

    // Simple threshold rules matching the training label boundaries
    MLResult infer(float tempC, float humPct, float smokePct) {
        MLResult r = {};
        bool warn = (tempC > 35.0f || humPct > 80.0f || smokePct > 30.0f);
        bool crit = (tempC > 45.0f || smokePct > 60.0f);
        r.pCritical = crit ? 1.0f : 0.0f;
        r.pWarning  = (!crit && warn) ? 1.0f : 0.0f;
        r.pNormal   = (!crit && !warn) ? 1.0f : 0.0f;
        r.riskScore = r.pWarning * 0.5f + r.pCritical;
        r.label     = crit ? 2 : (warn ? 1 : 0);
        r.valid     = true;
        return r;
    }
};

#endif  // ML_DISABLED
