#include <stddef.h>
#include <cstring>
#include <algorithm>
#include <vector>
#include <picojson.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <esp_http_client.h>
#include <freertos/task.h>

#include "utils.h"

// use GPIO-1 if sdcard in 4-bit mode (ttl is sacrifised)
// or GPIO-13 if in 1-bit mode or without sdcard
static const int servoPin = 13;

#ifndef ARDUINO
#include "iot_servo.h"

static double _read_angle(void)
{
    float angle;
    ESP_ERROR_CHECK(iot_servo_read_angle(LEDC_LOW_SPEED_MODE, 0, &angle));
    return angle;
}

static void _write_angle(double angle)
{
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, angle));
}

static void _init(void)
{
    servo_config_t servo_cfg = {
        .max_angle = 90,
        .min_width_us = 500,
        .max_width_us = 2500,
        .freq = 50,
        .timer_number = LEDC_TIMER_0,
        .channels = {
            .servo_pin = {
                (gpio_num_t) servoPin,
            },
            .ch = {
                LEDC_CHANNEL_0,
            },
        },
        .channel_number = 1,
    };
    ESP_ERROR_CHECK(iot_servo_init(LEDC_LOW_SPEED_MODE, &servo_cfg));
}

#else
#include "Servo.h"

Servo servo;

static void _init(void)
{
    servo.attach(servoPin, Servo::CHANNEL_NOT_ATTACHED, 0, 90);
}

static void _write_angle(double angle)
{
    servo.write((int)angle);
}

static double _read_angle(void)
{
    return (double) servo.read();
}

#endif

static const char * TAG = "gripper";

extern double get_timestamp();

const int syncFPS = 10;
const int QUEUE_THRESHOLD = syncFPS;

void onManipulatorCommand(void *message, size_t size, size_t offset, size_t total)
{
    char buffer[64];
    size_t len = std::min(size, (size_t) 63);
    strncpy(buffer, (const char *)message, len);
    buffer[len] = 0;
    ESP_LOGI(TAG, "mqtt, receiving command: %s", buffer);

    float angle = std::max(0.f, std::min(180.f, std::stof(buffer)));
    ESP_LOGI(TAG, "mqtt, setting angle: %f", angle);

    _write_angle(angle);
}

double getManipulatorState(void)
{
    return _read_angle();
}

esp_err_t initManiplator(void)
{
    _init();
    ESP_LOGI(TAG, "servo: attached.");
    _write_angle(1);
    return ESP_OK;
}