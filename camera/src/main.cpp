#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <ostream>
#include <iomanip>
#include <base64.h>
#include <esp_timer.h>
#include <esp_sntp.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <nvs_flash.h>

#include "pins.h"
#include "utils.h"
#include "mqtt.h"
#include "manipulator.h"
#include "camera.h"
#include "httpd.h"

static const char * TAG = "main";

extern void onManipulatorCommand(void *message, size_t size, size_t offset, size_t total);
extern esp_err_t initWifi(void);
extern bool wifiConnected(void);

static MqttClient mqtt(CONFIG_MQTT_ENDPOINT);

#if defined(LED_PIN)
static void initLED()
{
    ESP_LOGI(TAG, "setting up LED");
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

void initMqtt()
{
    mqtt.connect();
    mqtt.subscribe("/manipulator/command", onManipulatorCommand);
    ESP_LOGI(TAG, "MQTT topic subscribed.");
}

void setup()
{
#ifdef ARDUINO
    Serial.begin(115200);
    Serial.setDebugOutput(true);
#endif

#ifdef LED_PIN
    initLED();
#endif

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(initWifi());

    initCamera();

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
        abort();
    }

    initMqtt();
    ESP_ERROR_CHECK(initManiplator());
    ESP_ERROR_CHECK(initHttpd());
}

void loop()
{
    if (!mqtt.connected() || !wifiConnected()) {
        ESP_LOGE(TAG, "disconnected...");
#ifdef LED_PIN
        flashLED(500);
#endif
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
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