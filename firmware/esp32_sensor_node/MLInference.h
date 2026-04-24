#pragma once
/*
 * MLInference.h
 * Runs TFLite Micro inference on-device to produce a risk score
 * from [temperature, humidity, light_level] inputs.
 *
 * The model is embedded as a C byte-array in tinyml_model.h.
 * No SD card or network required — runs fully offline.
 *
 * Input tensor:  float32[3]  → [temp_norm, hum_norm, light_norm]
 *                              all normalised to [0, 1]
 * Output tensor: float32[3]  → [p_normal, p_warning, p_critical]
 *                              softmax probabilities summing to 1.0
 *
 * Risk score (0.0–1.0) = p_warning * 0.5 + p_critical * 1.0
 */

#include <Arduino.h>
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

struct MLResult {
    float pNormal;    // probability of normal class
    float pWarning;   // probability of warning class
    float pCritical;  // probability of critical class
    float riskScore;  // weighted composite: 0.0 (safe) – 1.0 (critical)
    uint8_t label;    // 0=normal, 1=warning, 2=critical
    bool    valid;    // false if inference failed
};

class MLInference {
public:
    MLInference();
    ~MLInference();

    // Call once in setup() — loads model, allocates tensor arena
    bool begin();

    // Run inference; inputs are raw sensor values (normalisation done inside)
    MLResult infer(float tempC, float humPct, float lightNorm);

    // Normalisation bounds — update if you retrain on different data ranges
    static constexpr float TEMP_MIN  = -10.0f;
    static constexpr float TEMP_MAX  =  60.0f;
    static constexpr float HUM_MIN   =   0.0f;
    static constexpr float HUM_MAX   = 100.0f;

private:
    // TFLite Micro objects
    const tflite::Model*          _model;
    tflite::MicroInterpreter*     _interpreter;
    TfLiteTensor*                 _inputTensor;
    TfLiteTensor*                 _outputTensor;

    // Working memory for the interpreter — 8 KB is enough for a small MLP
    static constexpr size_t TENSOR_ARENA_SIZE = 8 * 1024;
    uint8_t _tensorArena[TENSOR_ARENA_SIZE];

    bool _ready;

    float _normalise(float val, float minVal, float maxVal) const;
};
