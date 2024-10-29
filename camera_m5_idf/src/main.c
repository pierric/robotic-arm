#include <esp_err.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_sleep.h>
#include <nvs_flash.h>
#include <time.h>

#include "m5stack_camera.h"
#include "bm8563.h"
#include "esp_camera.h"

static const char *TAG = "MAIN";
static RTC_DATA_ATTR bool time_synced = false;
const gpio_num_t EXT_WAKEUP_PIN = (gpio_num_t) 13;

extern esp_err_t initWifi(void);
extern esp_err_t initHttpd();

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sscb_sda = CAM_PIN_SIOD,
    .pin_sscb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, //YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_VGA,    //QQVGA-UXGA Do not use sizes above QVGA when not JPEG

    .jpeg_quality = 16,
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST,
    .sccb_i2c_port = 0,
};

esp_err_t sync_time() {
    extern i2c_dev_t bm8563_dev;
    if (time_synced) {
        struct tm t_st;
        bool valid = false;
        esp_err_t rc = bm8563_get_time(&bm8563_dev, &t_st, &valid);
        if (rc == ESP_OK && valid) {
            // mktime(3) uses localtime, force UTC
            char* oldtz = getenv("TZ");
            setenv("TZ", "GMT0", 1);
            tzset();  // Workaround for https://github.com/espressif/esp-idf/issues/11455
            struct timeval now;
            now.tv_sec = mktime(&t_st);
            if (oldtz) {
                setenv("TZ", oldtz, 1);
            } else {
                unsetenv("TZ");
            }
            now.tv_usec = 0;
            settimeofday(&now, NULL);
            return ESP_OK;
        }
    }

    ESP_LOGI(TAG, "initializing SNTP.");
    sntp_set_sync_interval(2 * 3600 * 1000);
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_init();

    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "... Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        ESP_LOGI(TAG, "... done");
    }
    else {
        ESP_LOGI(TAG, "... still not ready");
        return ESP_FAIL;
    }

    time_t now;
    time(&now);
    bm8563_set_time(&bm8563_dev, localtime(&now));
    time_synced = true;
    return ESP_OK;
}

void app_main() {
    vTaskDelay(200 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(m5_camera_init());
    ESP_ERROR_CHECK(m5_camera_battery_hold_power());
    gpio_deep_sleep_hold_en();
    gpio_hold_en((gpio_num_t) BAT_HOLD_PIN);
    gpio_set_direction(EXT_WAKEUP_PIN, GPIO_MODE_INPUT);
    esp_sleep_enable_ext0_wakeup(EXT_WAKEUP_PIN, 1);

    ESP_LOGI(TAG, "Battery voltage: %dmV", m5_camera_battery_voltage());
    ESP_ERROR_CHECK(m5_camera_led_set_brightness(64));

    if (m5_camera_check_rtc_alarm_flag()) {
        m5_camera_clear_rtc_alarm_flag();
    }
    if (m5_camera_check_rtc_timer_flag()) {
        m5_camera_clear_rtc_timer_flag();
    }    

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_camera_init(&camera_config));

    sensor_t *sensor = esp_camera_sensor_get();
    sensor->set_pixformat(sensor, PIXFORMAT_JPEG);
    sensor->set_framesize(sensor, FRAMESIZE_VGA);
    sensor->set_vflip(sensor, 1);
    sensor->set_hmirror(sensor, 0);

    ESP_ERROR_CHECK(initWifi());
    setenv("TZ", "UTC", 1);
    tzset();
    ESP_ERROR_CHECK(sync_time());
    ESP_ERROR_CHECK(initHttpd());

    while(true) {
        int voltage = m5_camera_battery_voltage();
        int level = (voltage - 3300) * 100 / (float)(4150 - 3350);
        level = (level < 0) ? 0 : (level >= 100) ? 100 : level;
        ESP_LOGI(TAG, "Voltage level: %d%%", level);

        if (gpio_get_level(EXT_WAKEUP_PIN) == 0) {
            ESP_LOGI(TAG, "Going to sleep now");
            esp_deep_sleep_start();
        }

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}