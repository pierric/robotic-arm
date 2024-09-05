#include <cstring>
#include <string>
#include <ostream>
#include <iomanip>
#include <esp_camera.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "utils.h"
#include "pins.h"
#include "sdcard.h"

static const char * TAG = "camera";

// Camera module bus communications frequency.
// Originally: config.xclk_freq_mhz = 20000000, but this lead to visual
// artifacts on many modules. See
// https://github.com/espressif/esp32-camera/issues/150#issuecomment-726473652
// et al.
#if !defined(XCLK_FREQ_MHZ)
static const unsigned long xCameraClk = 8;
#else
static const unsigned long xCameraClk = XCLK_FREQ_MHZ;
#endif

static const float FPS = 30.;

static camera_config_t config;
static int camera_enabled = 0;

static uint64_t last_timestamp = 0;
static uint64_t fps_counter_last_timestamp = 0;
static uint32_t fps = 0;
static uint64_t elasp_statistics = 0;

void initCamera()
{
    config.ledc_channel = LEDC_CHANNEL_1;
    config.ledc_timer = LEDC_TIMER_1;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 8 * 1000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count = 8;
    config.grab_mode = CAMERA_GRAB_LATEST;

#if defined(CAMERA_MODEL_ESP_EYE)
    pinMode(13, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);
#endif

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        abort();
    } else {
        ESP_LOGI(TAG, "Camera init succeeded");

        // Get a reference to the sensor
        sensor_t* s = esp_camera_sensor_get();
        ESP_LOGI(TAG, "Camera module: %d", s->id.PID);
        s->set_vflip(s, 1);           // 0 = disable , 1 = enable

        /*
         * Add any other defaults you want to apply at startup here:
         * uncomment the line and set the value as desired (see the comments)
         *
         * these are defined in the esp headers here:
         * https://github.com/espressif/esp32-camera/blob/master/driver/include/sensor.h#L149
         */

        // s->set_framesize(s, FRAMESIZE_SVGA); //
        // FRAMESIZE_[QQVGA|HQVGA|QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA|QXGA(ov3660)]);
        // s->set_quality(s, val);       // 10 to 63
        // s->set_brightness(s, 0);      // -2 to 2
        // s->set_contrast(s, 0);        // -2 to 2
        // s->set_saturation(s, 0);      // -2 to 2
        // s->set_special_effect(s, 0);  // 0 to 6 (0 - No Effect, 1 - Negative,
        // 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 -
        // Sepia) s->set_whitebal(s, 1);        // aka 'awb' in the UI; 0 =
        // disable , 1 = enable s->set_awb_gain(s, 1);        // 0 = disable , 1
        // = enable s->set_wb_mode(s, 0);         // 0 to 4 - if awb_gain
        // enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
        // s->set_exposure_ctrl(s, 1);
        // // 0 = disable , 1 = enable s->set_aec2(s, 0);            // 0 =
        // disable , 1 = enable s->set_ae_level(s, 0);        // -2 to 2
        // s->set_aec_value(s, 300);     // 0 to 1200 s->set_gain_ctrl(s, 1); //
        // 0 = disable , 1 = enable s->set_agc_gain(s, 0);        // 0 to 30
        // s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6 s->set_bpc(s, 0);
        // // 0 = disable , 1 = enable s->set_wpc(s, 1);             // 0 =
        // disable , 1 = enable s->set_raw_gma(s, 1);         // 0 = disable , 1
        // = enable s->set_lenc(s, 1);            // 0 = disable , 1 = enable
        // s->set_hmirror(s, 0);         // 0 = disable , 1 = enable
        // s->set_dcw(s, 1);             // 0 = disable , 1 = enable
        // s->set_colorbar(s, 0);        // 0 = disable , 1 = enable
    }
}

static char *buffer;

esp_err_t camera_capture(double now)
{
    FILE* file = nullptr;
    std::string fname, fnameTmp;
    std::ostringstream fnamebuilder;
    fnamebuilder << "/sdcard/" << std::fixed << std::setprecision(3) << now;

    fnameTmp = fnamebuilder.str() + ".tmp";
    fname = fnamebuilder.str() + ".jpg";

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        // fclose(file);
        ESP_LOGI(TAG, "Camera capture failed");
        return ESP_FAIL;
    }
    size_t len = fb->len;
    assert(fb->len < 128*1024);
    memcpy(buffer, fb->buf, fb->len);
    esp_camera_fb_return(fb);

    file = fopen(fnameTmp.c_str(), "w");
    if (!file) {
        ESP_LOGW(TAG, "Failed to write file '%s', errno: %d", fnameTmp.c_str(), errno);
        return ESP_FAIL;
    }
    fwrite(buffer, len, 1, file);
    fclose(file);    

    if (0 != rename(fnameTmp.c_str(), fname.c_str())) {
        ESP_LOGW(TAG, "Failed to rename file '%s' to '%s'", fnameTmp.c_str(), fname.c_str());
    }

    return ESP_OK;
}

void onCameraCommand(void *message, size_t size, size_t offset, size_t total)
{
    if (strncmp((const char *)message, "on", 2) == 0) {
        ESP_LOGI(TAG, "enabling camera");
        camera_enabled = 1;
    }
    else {
        ESP_LOGI(TAG, "disabling camera");
        camera_enabled = 0;
    }
}

void cameraTask(void * pvParameters)
{
    buffer = (char *)malloc(128 * 1024);
    while(true) {
        if (camera_enabled && has_sdcard()) {
            camera_capture(get_timestamp());
        }

        uint64_t end = esp_timer_get_time();
        uint64_t elasp = end - last_timestamp;
        elasp_statistics += elasp;
        float perframe = (1000000. / FPS);
        if (elasp < perframe) {
            vTaskDelay((perframe - elasp) / (1000.0 * portTICK_PERIOD_MS));
        }
        last_timestamp = esp_timer_get_time();

        if (last_timestamp - fps_counter_last_timestamp > 5000000) {
            ESP_LOGI(TAG, "fps: %d, avg_elasp: %f", int(fps/5), elasp_statistics / (fps+0.01));
            fps = 0;
            elasp_statistics = 0;
            fps_counter_last_timestamp = last_timestamp;
        }
        fps += 1;
    }
}