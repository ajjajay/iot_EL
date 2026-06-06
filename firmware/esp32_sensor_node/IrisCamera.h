#pragma once
/*
 * IrisCamera.h
 * Wraps the ESP32-CAM OV2640 driver to capture iris images and extract
 * a compact 64-element feature vector via 8×8 zonal mean-intensity.
 *
 * Hardware target: AI Thinker ESP32-CAM (OV2640, 4 MB PSRAM)
 * Library: esp_camera (bundled with ESP32 Arduino core ≥ 2.0.11)
 *
 * Feature extraction pipeline:
 *   1. Capture QVGA (320×240) greyscale frame
 *   2. Crop central 240×240 region (iris is roughly centred)
 *   3. Divide into 8×8 grid → 64 cells
 *   4. Compute normalised mean intensity per cell → float32[64]
 *
 * For production iris recognition replace _extractFeatures() with a
 * proper segmentation + Gabor-wavelet pipeline, or proxy to
 * AWS Rekognition via the AWSIoTManager.
 */

#include <Arduino.h>
#include "esp_camera.h"

static constexpr uint8_t IRIS_FEAT_DIM  = 64;   // 8×8 zonal descriptor length
static constexpr uint8_t IRIS_GRID_SIDE = 8;    // grid dimension (GRID_SIDE² = FEAT_DIM)

struct IrisCapture {
    float    features[IRIS_FEAT_DIM];
    bool     valid;
    uint32_t timestampMs;
};

// Raw JPEG frame for upload to Firebase Storage → AWS Rekognition.
// Call freeJpeg() when done — buf is heap-allocated by captureJpeg().
struct JpegCapture {
    uint8_t* buf;
    size_t   len;
    bool     valid;
};

class IrisCamera {
public:
    // Default pin mapping: AI Thinker ESP32-CAM
    IrisCamera(int8_t pwdn  = 32, int8_t reset = -1,
               int8_t xclk  =  0, int8_t siod  = 26, int8_t sioc = 27,
               int8_t d7    = 35, int8_t d6    = 34, int8_t d5   = 39,
               int8_t d4    = 36, int8_t d3    = 21, int8_t d2   = 19,
               int8_t d1    = 18, int8_t d0    =  5,
               int8_t vsync = 25, int8_t href  = 23, int8_t pclk = 22);

    // Call once after WiFi is up; returns false if camera not detected
    bool begin();

    // Capture one frame and extract feature vector
    IrisCapture capture();

    // Average numFrames captures for a more stable enrollment template;
    // yields every delayMs to keep MQTT/RTOS tasks alive
    IrisCapture captureAverage(uint8_t numFrames = 5, uint32_t delayMs = 200);

    // Capture one JPEG frame (switches sensor to JPEG mode, grabs, switches back).
    // Returns heap-allocated buf — caller MUST call freeJpeg() when done.
    JpegCapture captureJpeg();
    static void freeJpeg(JpegCapture& jpeg);

    bool isReady() const { return _ready; }

private:
    camera_config_t _cfg;
    bool            _ready;

    // Extract 8×8 zonal mean-intensity descriptor from a greyscale buffer
    void _extractFeatures(const uint8_t* gray, uint16_t w, uint16_t h,
                           float* out) const;
};
