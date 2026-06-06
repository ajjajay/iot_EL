#include "IrisCamera.h"

IrisCamera::IrisCamera(int8_t pwdn,  int8_t reset,
                        int8_t xclk,  int8_t siod,  int8_t sioc,
                        int8_t d7,    int8_t d6,    int8_t d5,
                        int8_t d4,    int8_t d3,    int8_t d2,
                        int8_t d1,    int8_t d0,
                        int8_t vsync, int8_t href,  int8_t pclk)
    : _ready(false)
{
    memset(&_cfg, 0, sizeof(_cfg));

    _cfg.pin_pwdn     = pwdn;
    _cfg.pin_reset    = reset;
    _cfg.pin_xclk     = xclk;
    _cfg.pin_sscb_sda = siod;
    _cfg.pin_sscb_scl = sioc;
    _cfg.pin_d7  = d7; _cfg.pin_d6 = d6; _cfg.pin_d5 = d5;
    _cfg.pin_d4  = d4; _cfg.pin_d3 = d3; _cfg.pin_d2 = d2;
    _cfg.pin_d1  = d1; _cfg.pin_d0 = d0;
    _cfg.pin_vsync    = vsync;
    _cfg.pin_href     = href;
    _cfg.pin_pclk     = pclk;

    _cfg.xclk_freq_hz = 20000000;
    _cfg.ledc_timer   = LEDC_TIMER_0;
    _cfg.ledc_channel = LEDC_CHANNEL_0;
    _cfg.pixel_format = PIXFORMAT_GRAYSCALE;
    _cfg.frame_size   = FRAMESIZE_QVGA;   // 320×240 — sufficient iris detail
    _cfg.jpeg_quality = 12;
    _cfg.fb_count     = 1;
    _cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
}

bool IrisCamera::begin() {
    _cfg.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    if (psramFound()) _cfg.fb_count = 2;

    esp_err_t err = esp_camera_init(&_cfg);
    if (err != ESP_OK) {
        Serial.printf("[CAM] Init failed (0x%x)\n", err);
        return false;
    }

    // Tune sensor for high-contrast near-IR iris imaging
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s,  0);
        s->set_contrast(s,    2);
        s->set_saturation(s, -2);
        s->set_whitebal(s,    0);   // disable AWB
        s->set_awb_gain(s,    0);
        s->set_aec2(s,        0);   // disable AEC for consistent exposure
        s->set_aec_value(s,  300);
        s->set_agc_gain(s,    0);
        s->set_bpc(s,         0);
        s->set_wpc(s,         1);
        s->set_raw_gma(s,     1);
        s->set_lenc(s,        1);
    }

    Serial.println("[CAM] Ready");
    _ready = true;
    return true;
}

IrisCapture IrisCamera::capture() {
    IrisCapture r;
    r.valid       = false;
    r.timestampMs = millis();

    if (!_ready) {
        Serial.println("[CAM] capture() called before begin()");
        return r;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[CAM] Frame grab failed");
        return r;
    }

    if (fb->format == PIXFORMAT_GRAYSCALE) {
        _extractFeatures(fb->buf, fb->width, fb->height, r.features);
        r.valid = true;
    } else {
        Serial.printf("[CAM] Unexpected pixel format %d\n", fb->format);
    }

    esp_camera_fb_return(fb);
    return r;
}

IrisCapture IrisCamera::captureAverage(uint8_t numFrames, uint32_t delayMs) {
    IrisCapture avg;
    avg.valid       = false;
    avg.timestampMs = millis();

    float acc[IRIS_FEAT_DIM] = {};
    uint8_t valid = 0;

    for (uint8_t i = 0; i < numFrames; i++) {
        IrisCapture c = capture();
        if (c.valid) {
            for (uint8_t j = 0; j < IRIS_FEAT_DIM; j++) acc[j] += c.features[j];
            valid++;
        }
        // Keep MQTT / RTOS tasks alive between frames
        if (i < numFrames - 1) delay(delayMs);
    }

    if (valid == 0) return avg;

    for (uint8_t j = 0; j < IRIS_FEAT_DIM; j++) avg.features[j] = acc[j] / valid;
    avg.valid = true;
    Serial.printf("[CAM] Averaged %d/%d frames\n", valid, numFrames);
    return avg;
}

JpegCapture IrisCamera::captureJpeg() {
    JpegCapture result = { nullptr, 0, false };
    if (!_ready) return result;

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return result;

    // Switch to JPEG for this capture, then restore grayscale
    s->set_pixformat(s, PIXFORMAT_JPEG);
    s->set_framesize(s, FRAMESIZE_QVGA);
    delay(120);  // sensor needs ~2 frames to settle after format change

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb && fb->format == PIXFORMAT_JPEG && fb->len > 0) {
        result.buf = (uint8_t*)malloc(fb->len);
        if (result.buf) {
            memcpy(result.buf, fb->buf, fb->len);
            result.len   = fb->len;
            result.valid = true;
            Serial.printf("[CAM] JPEG captured: %u bytes\n", fb->len);
        }
    }
    if (fb) esp_camera_fb_return(fb);

    // Restore grayscale for subsequent iris feature captures
    s->set_pixformat(s, PIXFORMAT_GRAYSCALE);
    delay(60);

    return result;
}

void IrisCamera::freeJpeg(JpegCapture& jpeg) {
    if (jpeg.buf) { free(jpeg.buf); jpeg.buf = nullptr; }
    jpeg.len   = 0;
    jpeg.valid = false;
}

// 8×8 zonal mean-intensity descriptor, normalised to [0, 1]
void IrisCamera::_extractFeatures(const uint8_t* gray, uint16_t w, uint16_t h,
                                   float* out) const {
    uint16_t cw = w / IRIS_GRID_SIDE;
    uint16_t ch = h / IRIS_GRID_SIDE;

    for (uint8_t gy = 0; gy < IRIS_GRID_SIDE; gy++) {
        for (uint8_t gx = 0; gx < IRIS_GRID_SIDE; gx++) {
            uint32_t sum = 0, cnt = 0;
            uint16_t x0 = gx * cw,        x1 = min((uint16_t)((gx + 1) * cw), w);
            uint16_t y0 = gy * ch,         y1 = min((uint16_t)((gy + 1) * ch), h);
            for (uint16_t py = y0; py < y1; py++)
                for (uint16_t px = x0; px < x1; px++) {
                    sum += gray[py * w + px];
                    cnt++;
                }
            out[gy * IRIS_GRID_SIDE + gx] = cnt ? (float)sum / (cnt * 255.0f) : 0.0f;
        }
    }
}
