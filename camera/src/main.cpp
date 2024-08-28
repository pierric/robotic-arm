#include <Arduino.h>
#include <Elog.h>
#include <WiFi.h>
#include <base64.h>
#include <esp_camera.h>
#include <esp_http_client.h>
#include <esp_sntp.h>
// #include <json.hpp>
#include <picojson.h>
#include "driver/gpio.h"

#include "pins.h"
#include "mqtt.h"
#include "robot.h"

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

const float FPS = 30.;

static camera_config_t config;
static esp_http_client_handle_t http_client;
static MqttClient mqtt(Q(MQTT_ENDPOINT));
Elog logger;

extern void onManipulatorCommand(void *message, size_t size, size_t offset, size_t total);
extern void onCameraCommand(void *message, size_t size, size_t offset, size_t total);

extern esp_http_client_handle_t initHttpClient();
extern void initServo();

static int camera_enabled = 0;

static void flashLED(int flashtime)
{
#if defined(LED_PIN) // If we have it; flash it.
    digitalWrite(LED_PIN, LED_ON); // On at full power.
    delay(flashtime); // delay
    digitalWrite(LED_PIN, LED_OFF); // turn Off
#endif
}

static void initWifi()
{
    logger.log(INFO, "Starting WiFi");

    WiFi.setSleep(false);

    byte mac[6] = { 0, 0, 0, 0, 0, 0 };
    WiFi.macAddress(mac);
    logger.log(INFO, "MAC address: %02X:%02X:%02X:%02X:%02X:%02X", mac[0],
        mac[1], mac[2], mac[3], mac[4], mac[5]);

    logger.log(INFO, "Connecting to %s", Q(WIFI_SSID));
    WiFi.setHostname("parol6-camera");
    WiFi.begin(Q(WIFI_SSID), Q(WIFI_PASSWORD));

    unsigned long start = millis();
    while ((millis() - start <= WIFI_WATCHDOG)
        && (WiFi.status() != WL_CONNECTED)) {
        delay(500);
        logger.log(INFO, "... waiting");
    }

    if (WiFi.status() == WL_CONNECTED) {
        logger.log(INFO, "... succeeded");
    } else {
        logger.log(INFO, "... failed");
        WiFi.disconnect(); // (resets the WiFi scan)
    }
}

static void initCamera()
{
    // Populate camera config structure with hardware and other defaults
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
    // Low(ish) default framesize and quality
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
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
        logger.log(INFO, "Camera init succeeded");

        // Get a reference to the sensor
        sensor_t* s = esp_camera_sensor_get();
        logger.log(INFO, "Camera module: %d\n", s->id.PID);
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
    logger.log(INFO, "MQTT topic subscribed.");
}

void initCan();

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    Serial.println();
    Serial.println("====");
    logger.addSerialLogging(Serial, "Mani", INFO);

    if (!psramFound()) {
        Serial.println("Fatal Error; Halting");
        while (true) {
            Serial.println(
                "No PSRAM found; camera cannot be initialised: Please "
                "check the board config for your module.");
            delay(5000);
        }
    }

#if defined(LED_PIN) // If we have a notification LED, set it to output
    pinMode(LED_PIN, OUTPUT);
#endif

    flashLED(500);

    initCamera();

    while ((WiFi.status() != WL_CONNECTED)) {
        initWifi();
        delay(1000);
    }

    delay(1000);

    http_client = initHttpClient();

    logger.log(INFO, "initializing SNTP.");
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
        logger.log(INFO, "... Waiting for system time to be set... (%d/%d)", retry, retry_count);
        delay(2000);
    }
    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
        logger.log(INFO, "... done");
    }
    else {
        logger.log(INFO, "... still not ready");
    }

    initMqtt();
    initServo();
    initCan();
}

void onCameraCommand(void *message, size_t size, size_t offset, size_t total)
{
    if (strncmp((const char *)message, "on", 2) == 0) {
        logger.log(INFO, "enabling camera");
        camera_enabled = 1;
    }
    else {
        logger.log(INFO, "disabling camera");
        camera_enabled = 0;
    }
}


esp_err_t camera_capture(double now, struct RobotStatus& robot_status)
{
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        esp_camera_fb_return(fb);
        logger.log(INFO, "Camera capture failed");
        return ESP_FAIL;
    }

    logger.log(INFO, "status: %s", dump_status(robot_status).c_str());

    assert(fb->format == PIXFORMAT_JPEG);

    std::string bs = Base64::encode(std::string((const char*)fb->buf, fb->len));

    std::vector<picojson::value> positions;
    std::transform(
        robot_status.positions.begin(),
        robot_status.positions.end(),
        std::back_insert_iterator(positions),
        [](auto pos) {return picojson::value(pos);});

    picojson::object obj;
    obj["time_stamp"] = picojson::value((double)(now * 1000));
    obj["homed"] = picojson::value(robot_status.homed);
    obj["positions"] = picojson::value(positions);
    obj["gripper"] = picojson::value(robot_status.gripper_position);
    obj["image"] = picojson::value(std::move(bs));
    std::string payload = picojson::value(obj).serialize();

    esp_http_client_set_header(http_client, "Content-Type", "application/json");
    esp_http_client_set_method(http_client, HTTP_METHOD_POST);
    esp_http_client_set_url(http_client, "/camera");
    esp_http_client_set_post_field(
        http_client, payload.c_str(), payload.length());

    esp_err_t err = esp_http_client_perform(http_client);

    if (err != ESP_OK) {
        logger.log(INFO, "HTTP POST request failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    int rc = esp_http_client_get_status_code(http_client);
    if (rc != 200 && rc != 201) {
        logger.log(INFO, "Request error, status_code=%d: ", rc);
        size_t len = esp_http_client_get_content_length(http_client);
        char* buffer = (char*)malloc(len + 1);
        esp_http_client_read_response(http_client, buffer, len);
        buffer[len] = 0;
        logger.log(INFO, buffer);
        free(buffer);
    }

    esp_camera_fb_return(fb);
    return ESP_OK;
}

double get_timestamp()
{
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    return tv_now.tv_sec + (double)tv_now.tv_usec / 1000000.0f;
}

static uint64_t last_timestamp = 0;
static uint64_t fps_counter_last_timestamp = 0;
static uint32_t fps = 0;
static uint64_t elasp_statistics = 0;

void loop()
{
    esp_err_t rc = ESP_OK;
    double now = get_timestamp();

    auto robot_status = query_status();

    if (camera_enabled && robot_status) {
        rc = camera_capture(now, robot_status.value());
    }

    if (!robot_status.has_value() || rc != ESP_OK || !mqtt.connected() || WiFi.status() != WL_CONNECTED) {
        flashLED(500);
    }

    uint64_t end = esp_timer_get_time();
    uint64_t elasp = end - last_timestamp;
    elasp_statistics += elasp;
    float perframe = (1000000. / FPS);
    if (elasp < perframe) {
        delay((perframe - elasp) / 1000.0);
    }
    last_timestamp = end;

    if (end - fps_counter_last_timestamp > 1000000) {
        logger.log(INFO, "fps: %d, avg_elasp: %f", fps, elasp_statistics / (fps+0.01));
        fps = 0;
        elasp_statistics = 0;
        fps_counter_last_timestamp = end;
    }
    fps += 1;
}