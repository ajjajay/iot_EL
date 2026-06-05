#include "MLInference.h"

#ifndef ML_DISABLED

#include "tinyml_model.h"   // g_model[] byte array

MLInference::MLInference() : _model(nullptr), _interpreter(nullptr),
                              _inputTensor(nullptr), _outputTensor(nullptr),
                              _ready(false) {}

MLInference::~MLInference() {
    delete _interpreter;
}

bool MLInference::begin() {
    Serial.println("[ML] Loading TFLite model...");

    _model = tflite::GetModel(g_model);
    if (_model->version() != TFLITE_SCHEMA_VERSION) {
        Serial.printf("[ML] Model schema mismatch: got %u, expected %d\n",
                      _model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    // Register only the ops actually used by the model (saves flash & RAM)
    static tflite::MicroMutableOpResolver<5> resolver;
    resolver.AddFullyConnected();
    resolver.AddRelu();
    resolver.AddSoftmax();
    resolver.AddReshape();
    resolver.AddQuantize();

    static tflite::MicroInterpreter interpreter(
        _model, resolver, _tensorArena, TENSOR_ARENA_SIZE, nullptr, nullptr, nullptr);
    _interpreter = &interpreter;

    TfLiteStatus allocStatus = _interpreter->AllocateTensors();
    if (allocStatus != kTfLiteOk) {
        Serial.println("[ML] AllocateTensors() failed");
        return false;
    }

    _inputTensor  = _interpreter->input(0);
    _outputTensor = _interpreter->output(0);

    // Sanity check tensor shapes
    if (_inputTensor->dims->data[1] != 3 || _outputTensor->dims->data[1] != 3) {
        Serial.printf("[ML] Unexpected tensor dims: in=%d out=%d\n",
                      _inputTensor->dims->data[1], _outputTensor->dims->data[1]);
        return false;
    }

    size_t usedBytes = _interpreter->arena_used_bytes();
    Serial.printf("[ML] Model loaded OK — arena used: %u / %u bytes\n",
                  usedBytes, TENSOR_ARENA_SIZE);
    _ready = true;
    return true;
}

MLResult MLInference::infer(float tempC, float humPct, float lightNorm) {
    MLResult result = {0};
    result.valid = false;

    if (!_ready) {
        Serial.println("[ML] Inference called before begin()");
        return result;
    }

    // Populate normalised inputs
    _inputTensor->data.f[0] = _normalise(tempC,  TEMP_MIN, TEMP_MAX);
    _inputTensor->data.f[1] = _normalise(humPct, HUM_MIN,  HUM_MAX);
    _inputTensor->data.f[2] = lightNorm;   // already 0–1

    TfLiteStatus invokeStatus = _interpreter->Invoke();
    if (invokeStatus != kTfLiteOk) {
        Serial.println("[ML] Invoke() failed");
        return result;
    }

    result.pNormal   = _outputTensor->data.f[0];
    result.pWarning  = _outputTensor->data.f[1];
    result.pCritical = _outputTensor->data.f[2];

    // Composite risk score: 0 = safe, 1 = critical
    result.riskScore = result.pWarning * 0.5f + result.pCritical * 1.0f;

    // Argmax for discrete label
    if (result.pNormal >= result.pWarning && result.pNormal >= result.pCritical)
        result.label = 0;
    else if (result.pWarning >= result.pCritical)
        result.label = 1;
    else
        result.label = 2;

    result.valid = true;

    Serial.printf("[ML] n=%.3f w=%.3f c=%.3f → risk=%.3f label=%d\n",
                  result.pNormal, result.pWarning, result.pCritical,
                  result.riskScore, result.label);
    return result;
}

float MLInference::_normalise(float val, float minVal, float maxVal) const {
    float norm = (val - minVal) / (maxVal - minVal);
    return constrain(norm, 0.0f, 1.0f);
}

#endif  // ML_DISABLED
