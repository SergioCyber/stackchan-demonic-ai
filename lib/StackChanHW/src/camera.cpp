#include "camera.h"
#include <M5Unified.h>
#include "esp_camera.h"
#include "img_converters.h"

// CoreS3 DVP pins. XCLK comes from a crystal on the camera flex; reset is on
// the AW9523 expander (P1_0); PWDN is tied to GND.
static constexpr uint8_t AW9523_ADDR = 0x58;

static void resetSensor() {
    M5.In_I2C.bitOff(AW9523_ADDR, 0x05, 0x01, 400000);  // P1_0 as output
    M5.In_I2C.bitOff(AW9523_ADDR, 0x03, 0x01, 400000);  // reset low
    delay(25);
    M5.In_I2C.bitOn(AW9523_ADDR, 0x03, 0x01, 400000);   // release, PLL settles
    delay(50);
}

bool cameraInit() {
    M5.Power.Axp2101.setALDO1(1800);   // DVDD
    M5.Power.Axp2101.setALDO2(2800);   // AVDD
    M5.Power.Axp2101.setALDO4(3300);   // IOVDD
    delay(20);
    resetSensor();
    M5.In_I2C.release();               // the camera's SCCB shares GPIO 11/12
    delay(20);

    camera_config_t c = {};
    c.pin_d0 = 39; c.pin_d1 = 40; c.pin_d2 = 41; c.pin_d3 = 42;
    c.pin_d4 = 15; c.pin_d5 = 16; c.pin_d6 = 48; c.pin_d7 = 47;
    c.pin_vsync = 46; c.pin_href = 38; c.pin_pclk = 45;
    c.pin_sccb_sda = 12; c.pin_sccb_scl = 11;
    c.pin_xclk = -1; c.pin_reset = -1; c.pin_pwdn = -1;
    c.xclk_freq_hz = 10000000;
    c.ledc_timer = LEDC_TIMER_0;
    c.ledc_channel = LEDC_CHANNEL_0;
    c.pixel_format = PIXFORMAT_RGB565;
    c.frame_size = FRAMESIZE_QVGA;
    c.jpeg_quality = 12;               // unused with RGB565
    c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.grab_mode = CAMERA_GRAB_LATEST;  // always the newest frame, never a stale one
    c.sccb_i2c_port = -1;

    // A dropped VSYNC must never try to log from the camera ISR.
    esp_log_level_set("cam_hal", ESP_LOG_NONE);
    esp_log_level_set("esp_camera", ESP_LOG_NONE);
    esp_log_level_set("camera", ESP_LOG_NONE);
    esp_log_level_set("gdma", ESP_LOG_NONE);

    esp_err_t err = esp_camera_init(&c);
    if (err != ESP_OK) {
        Serial.printf("[CAM] esp_camera_init failed: 0x%x (%s)\n", err, esp_err_to_name(err));
        return false;
    }
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        Serial.printf("[CAM] sensor PID 0x%02X\n", s->id.PID);
        s->set_vflip(s, 1);
        s->set_hmirror(s, 0);
    }
    return true;
}

static SemaphoreHandle_t s_camMutex = nullptr;
static uint8_t s_luma[160 * 98];
static volatile uint8_t s_motionScore = 0;
static volatile uint32_t s_lastCaptureMs = 0;

static void updatePreviewFromFb(const camera_fb_t* fb) {
    if (!fb || !fb->buf || fb->len < 320 * 196 * 2) return;
    uint32_t diffSum = 0;
    const uint8_t* src = fb->buf;
    for (int py = 0; py < 98; py++) {
        const int sy = (py * 2) + 20; // center crop vertically (y = 20..216)
        const uint8_t* row = src + (sy * 320 * 2);
        uint8_t* dst = s_luma + (py * 160);
        for (int px = 0; px < 160; px++) {
            const int sx = (159 - px) * 2; // mirror horizontally so it acts like a mirror on screen
            const uint8_t b0 = row[sx * 2];
            const uint8_t b1 = row[sx * 2 + 1];
            const uint8_t r = b0 & 0xF8;
            const uint8_t g = ((b0 & 0x07) << 5) | ((b1 & 0xE0) >> 3);
            const uint8_t b = (b1 & 0x1F) << 3;
            const uint8_t lum = (uint8_t)((r + (uint16_t)g * 2 + b) >> 2);
            int d = (int)lum - (int)dst[px];
            if (d < 0) d = -d;
            if (d > 18) diffSum += (uint32_t)d;
            dst[px] = lum;
        }
    }
    uint32_t score = diffSum / 3200;
    s_motionScore = (uint8_t)(score > 100 ? 100 : score);
    s_lastCaptureMs = millis();
}

bool cameraCaptureJpeg(uint8_t quality, uint8_t** jpg, size_t* len) {
    if (!s_camMutex) s_camMutex = xSemaphoreCreateMutex();
    if (xSemaphoreTake(s_camMutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        xSemaphoreGive(s_camMutex);
        return false;
    }
    updatePreviewFromFb(fb);
    bool ok = frame2jpg(fb, quality, jpg, len);
    esp_camera_fb_return(fb);
    xSemaphoreGive(s_camMutex);
    return ok;
}

void cameraStepPreview() {
    if (millis() - s_lastCaptureMs < 250) return;
    if (!s_camMutex) s_camMutex = xSemaphoreCreateMutex();
    if (xSemaphoreTake(s_camMutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
        updatePreviewFromFb(fb);
        esp_camera_fb_return(fb);
    }
    xSemaphoreGive(s_camMutex);
}

const uint8_t* cameraGetLumaPreview() { return s_luma; }
uint8_t cameraGetMotionScore() { return s_motionScore; }
