#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <base64.h>
#include <esp_camera.h>
#include <esp_timer.h>
#include <esp_http_client.h>
#include <esp_sntp.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <picojson.h>
#include <driver/gpio.h>
#include <nvs_flash.h>

#include "config.h"
#include "utils.h"
#include "pins.h"
#include "http.h"
#include "mqtt.h"
#include "manipulator.h"

#define QQ(x) #x
#define Q(x) QQ(x)

#if !defined(WIFI_WATCHDOG)
#define WIFI_WATCHDOG 15000
#endif

// Camera module bus communications frequency.
// Originally: config.xclk_freq_mhz = 20000000, but this lead to visual
// artifacts on many modules. See
// https://github.com/espressif/esp32-camera/issues/150#issuecomment-726473652
// et al.
#if !defined(XCLK_FREQ_MHZ)
unsigned long xclk = 8;
#else
unsigned long xclk = XCLK_FREQ_MHZ;
#endif

static const char * TAG = "main";

static const float FPS = 30.;
static esp_http_client_handle_t http_client;
static MqttClient mqtt(CONFIG_MQTT_ENDPOINT);

extern void onManipulatorCommand(void *message, size_t size, size_t offset, size_t total);
extern void onCameraCommand(void *message, size_t size, size_t offset, size_t total);
extern void initWifi(void);
extern bool wifiConnected(void);

static int camera_enabled = 0;

#if defined(LED_PIN)
static void initLED()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LED_PIN,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
}

static void flashLED(int flashtime)
{
    gpio_set_level((gpio_num_t)LED_PIN, LED_ON);
    vTaskDelay(flashtime / portTICK_PERIOD_MS);
    gpio_set_level((gpio_num_t)LED_PIN, LED_OFF);
}
#endif

static camera_config_t config;
static void initCamera()
{
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
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
    config.xclk_freq_hz = xclk * 1000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count = 2;
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

void initMqtt()
{
    mqtt.connect();
    mqtt.subscribe("/manipulator/command", onManipulatorCommand);
    mqtt.subscribe("/camera/command", onCameraCommand);
    ESP_LOGI(TAG, "MQTT topic subscribed.");
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

static void _send(esp_http_client_handle_t http_client, const std::vector<std::pair<double, std::string>>& queue)
{
    picojson::array batch;

    for (auto [timestamp, image]: queue) {
        picojson::object obj;
        obj["time_stamp"] = picojson::value(timestamp);
        obj["image"] = picojson::value(image);
        batch.push_back(picojson::value(obj));
    }

    std::string payload = picojson::value(batch).serialize();

    esp_http_client_set_header(http_client, "Content-Type", "application/json");
    esp_http_client_set_method(http_client, HTTP_METHOD_POST);
    esp_http_client_set_url(http_client, "/camera");
    esp_http_client_set_post_field(http_client, payload.c_str(), payload.length());

    esp_err_t err = esp_http_client_perform(http_client);

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }
    else {
        int rc = esp_http_client_get_status_code(http_client);
        if (rc != 200 && rc != 201) {
            ESP_LOGI(TAG, "HTTP error [%d]: %s", rc, getHttpContent(http_client).c_str());
        }
    }
}

esp_err_t camera_capture(double now)
{
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGI(TAG, "Camera capture failed");
        return ESP_FAIL;
    }

    assert(fb->format == PIXFORMAT_JPEG);

    std::string bs = Base64::encode(std::string((const char*)fb->buf, fb->len));
    _send(http_client, {std::make_pair((double)(now * 1000), bs)});

    esp_camera_fb_return(fb);
    return ESP_OK;
}

static uint64_t last_timestamp = 0;
static uint64_t fps_counter_last_timestamp = 0;
static uint32_t fps = 0;
static uint64_t elasp_statistics = 0;
static double delay_statistics = 0;

void setup()
{
#ifdef ARDUINO
    Serial.begin(115200);
    Serial.setDebugOutput(true);
#endif

    //heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);

#ifdef LED_PIN
    // initLED();
    pinMode(LED_PIN, OUTPUT);

#endif

    ESP_ERROR_CHECK(nvs_flash_init());
    initCamera();
    initWifi();
    http_client = initHttpClient();

    ESP_LOGI(TAG, "initializing SNTP.");
    setenv("TZ", "UTC", 2);
    tzset();
    sntp_set_sync_interval(2 * 3600 * 1000);
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_init();

    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET
        && ++retry < retry_count) {
        ESP_LOGI(TAG, "... Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        ESP_LOGI(TAG, "... done");
    }
    else {
        ESP_LOGI(TAG, "... still not ready");
    }

    initMqtt();
    // initManiplator();
}

void loop()
{
    esp_err_t rc = ESP_OK;
    double now = get_timestamp();

    if (camera_enabled) {
        rc = camera_capture(now);
    }

    if (rc != ESP_OK /* || !mqtt.connected() || !wifiConnected() */) {
        ESP_LOGE(TAG, "malfunctioning... %d", rc);
#ifdef LED_PIN            
        flashLED(500);
#endif            
    }

    uint64_t end = esp_timer_get_time();
    uint64_t elasp = end - last_timestamp;
    elasp_statistics += elasp;
    float perframe = (1000000. / FPS);
    if (elasp < perframe) {
        delay_statistics += (perframe - elasp) / 1000.0;
        vTaskDelay((perframe - elasp) / (1000.0 * portTICK_PERIOD_MS));
    }
    last_timestamp = esp_timer_get_time();

    if (last_timestamp - fps_counter_last_timestamp > 1000000) {
        ESP_LOGI(TAG, "fps: " FMT_UINT32_T ", avg_elasp: %f, avg_delay: %f", fps, elasp_statistics / (fps+0.01), delay_statistics / (fps+0.1));
        fps = 0;
        elasp_statistics = 0;
        delay_statistics = 0;
        fps_counter_last_timestamp = last_timestamp;
    }
    fps += 1;
}

#ifndef ARDUINO
extern "C" void app_main(void)
{
    setup();

    while(true) {
        loop();
    }
}
#endif