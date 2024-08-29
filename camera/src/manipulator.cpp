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

#include "iot_servo.h"
#include "http.h"

static const char * TAG = "gripper";

extern esp_http_client_handle_t initHttpClient();
extern double get_timestamp();

const int servoPin = 12;
const int syncFPS = 30;
const int QUEUE_THRESHOLD = syncFPS;
static TaskHandle_t xSyncHandle = NULL;

void onManipulatorCommand(void *message, size_t size, size_t offset, size_t total)
{
    char buffer[64];
    size_t len = std::min(size, (size_t) 63);
    strncpy(buffer, (const char *)message, len);
    buffer[len] = 0;
    ESP_LOGI(TAG, "mqtt, receiving command: %s", buffer);

    float angle = std::max(0.f, std::min(180.f, std::stof(buffer)));
    ESP_LOGI(TAG, "mqtt, setting angle: %f", angle);

    esp_err_t rc = iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, angle);

    if (ESP_OK != rc) {
        ESP_LOGE(TAG, "servo: failed to set angle: %s", esp_err_to_name(rc));
    }
}

double getManipulatorState(void)
{
    float angle;
    esp_err_t rc = iot_servo_read_angle(LEDC_LOW_SPEED_MODE, 0, &angle);

    if (ESP_OK != rc) {
        ESP_LOGE(TAG, "servo: cannot read the angle: %s", esp_err_to_name(rc));
        return 0;
    }

    return angle;
}

static void _send(esp_http_client_handle_t http_client, std::vector<std::pair<double, double>>& queue)
{
    picojson::array batch;

    for (auto [timestamp, position]: queue) {
        picojson::object obj;
        obj["time_stamp"] = picojson::value(timestamp);
        obj["position"] = picojson::value(position);
        batch.push_back(picojson::value(obj));
    }

    std::string payload = picojson::value(batch).serialize();
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

void vSyncTask(void * pvParameters)
{
    esp_http_client_handle_t http_client = initHttpClient();

    esp_http_client_set_header(http_client, "Content-Type", "application/json");
    esp_http_client_set_method(http_client, HTTP_METHOD_POST);
    esp_http_client_set_url(http_client, "/gripper");

    uint64_t last_timestamp = esp_timer_get_time();
    double perFrame = (1000000. / syncFPS);
    uint64_t fps_counter_last_timestamp = 0;
    uint32_t fps = 0;
    std::vector<std::pair<double, double>> queue;

    while(true) {
        queue.push_back(std::make_pair((double)(get_timestamp() * 1000), getManipulatorState()));

        if (queue.size() >= QUEUE_THRESHOLD) {
            _send(http_client, queue);
            queue.clear();
        }

        uint64_t elasp = esp_timer_get_time() - last_timestamp;
        if (elasp < perFrame) {
            vTaskDelay((perFrame - elasp) / (1000. * portTICK_PERIOD_MS));
        }
        last_timestamp = esp_timer_get_time(); 

        if (last_timestamp - fps_counter_last_timestamp > 1000000) {
            ESP_LOGD(TAG, "[Manipulator] fps: %lu", fps);
            fps = 0;
            fps_counter_last_timestamp = last_timestamp;
        }        
        fps += 1;
    }
}

esp_err_t initManiplator(void)
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
    esp_err_t rc = iot_servo_init(LEDC_LOW_SPEED_MODE, &servo_cfg);

    if (ESP_OK != rc) {
        ESP_LOGE(TAG, "servo: failed to initialize: %s", esp_err_to_name(rc));
        return rc;
    }

    ESP_LOGI(TAG, "servo: attached.");

    iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 0);

    xTaskCreate(vSyncTask, "SyncTask", 65536, NULL, tskIDLE_PRIORITY, &xSyncHandle);
    return ESP_OK;
}
