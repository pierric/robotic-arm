#include <cstring>
#include <esp_log.h>

#include "mqtt.h"
#include "utils.h"

static const char * TAG = "MQTT";

void MqttClient::_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    MqttClient *ptr = (MqttClient *)handler_args;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED.");
        ptr->_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED.");
        ptr->_connected = false;
        break;
    case MQTT_EVENT_DATA:
    {
        std::string topic(event->topic, event->topic_len);
        ESP_LOGI(TAG, "MQTT_EVENT_DATA. topic: %s (%d), data_len: %d, total_data_len: %d",
            topic.c_str(),
            event->topic_len,
            event->data_len,
            event->total_data_len
        );
        if (!ptr)
        {
            ESP_LOGI(TAG, "- no client instance");
            break;
        }

        if (event->current_data_offset == 0)
        {
            ptr->_messageTopic = topic;
        }

        MESSAGE_HANDLER handler = ptr->find_message_handler(event->topic_len > 0 ? topic.c_str() : ptr->_messageTopic.c_str());
        if (handler)
        {
            ESP_LOGI(TAG, "Message received");
            handler(event->data, event->data_len, event->current_data_offset, event->total_data_len);
        }
        else
        {
            ESP_LOGI(TAG, "Message discarded, no handler for the topic %s", topic.c_str());
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        // MQTT_EVENT_ANY, MQTT_EVENT_SUBSCRIBED, MQTT_EVENT_UNSUBSCRIBED, MQTT_EVENT_PUBLISHED
        // MQTT_EVENT_BEFORE_CONNECT, MQTT_EVENT_DELETED, MQTT_USER_EVENT
        break;
    }
}

MqttClient::MqttClient(const std::string& endpoint)
{
    const auto [host, port] = parseEndpoint(endpoint);
    this->_brokerHost = host;
    this->_brokerPort = port.value_or(1883);
}

void MqttClient::connect()
{
    if (this->_client != nullptr)
    {
        ESP_LOGI(TAG, "MQTT broker already connected.");
        return;
    }

    ESP_LOGI(TAG, "Connecting MQTT broker: %s:" FMT_UINT32_T, this->_brokerHost.c_str(), this->_brokerPort);

    // IDF_VER >= 5
    // const esp_mqtt_client_config_t mqtt_cfg = {
    //     .broker = {
    //         .address = {
    //             .uri = this->_brokerHost.c_str(),
    //             .port = this->_brokerPort,
    //         }
    //     },
    //     .buffer = {.size = 2560},
    // };

    // IDF_VER < 5
    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = this->_brokerHost.c_str(),
        .port = this->_brokerPort,
        .buffer_size = 2560,
    };

    this->_client = esp_mqtt_client_init(&mqtt_cfg);

    if (this->_client == nullptr) {
        ESP_LOGI(TAG, "Failed to init MQTT client");
        return;
    }

    esp_mqtt_client_register_event(this->_client, MQTT_EVENT_ANY, this->_event_handler, this);
    esp_mqtt_client_start(this->_client);

    while (!this->_connected)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void MqttClient::publish(const char *topic, void *data, size_t size)
{
    if (!this->_client) {
        ESP_LOGI(TAG, "MQTT connection not ready.");
        return;
    }

    int msg_id = esp_mqtt_client_publish(this->_client, topic, (const char *)data, size, 0, 0);
    if (msg_id < 0)
    {
        ESP_LOGI(TAG, "Failed to publish message.");
    }
}

void MqttClient::subscribe(const char *topic, MESSAGE_HANDLER handler)
{
    int msg_id = esp_mqtt_client_subscribe(this->_client, topic, 0);

    if (msg_id < 0)
    {
        ESP_LOGI(TAG, "Failed to subscribe topic: %s. rc: %d", topic, msg_id);
    }

    this->_handlers[topic] = handler;
}

MESSAGE_HANDLER MqttClient::find_message_handler(const std::string &topic)
{
    auto it = this->_handlers.find(topic);

    if (it == this->_handlers.end())
    {
        return nullptr;
    }

    return it->second;
}