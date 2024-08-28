#include <Elog.h>
#include <cstring>

#include "mqtt.h"
#include "utils.h"

extern Elog logger;

void MqttClient::_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    MqttClient *ptr = (MqttClient *)handler_args;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        logger.log(INFO, "MQTT_EVENT_CONNECTED.");
        ptr->_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        logger.log(INFO, "MQTT_EVENT_DISCONNECTED.");
        ptr->_connected = false;
        break;
    case MQTT_EVENT_DATA:
    {
        std::string topic(event->topic, event->topic_len);
        logger.log(INFO, "MQTT_EVENT_DATA. topic: %s (%d), data_len: %d, total_data_len: %d",
            topic.c_str(),
            event->topic_len,
            event->data_len,
            event->total_data_len
        );
        if (!ptr)
        {
            logger.log(INFO, "- no client instance");
            break;
        }

        if (event->current_data_offset == 0)
        {
            ptr->_messageTopic = topic;
        }

        MESSAGE_HANDLER handler = ptr->find_message_handler(event->topic_len > 0 ? topic.c_str() : ptr->_messageTopic.c_str());
        if (handler)
        {
            logger.log(INFO, "Message received");
            handler(event->data, event->data_len, event->current_data_offset, event->total_data_len);
        }
        else
        {
            logger.log(INFO, "Message discarded, no handler for the topic %s", topic.c_str());
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        logger.log(INFO, "MQTT_EVENT_ERROR");
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
        logger.log(INFO, "MQTT broker already connected.");
        return;
    }

    logger.log(INFO, "Connecting MQTT broker: %s:%d", this->_brokerHost.c_str(), this->_brokerPort);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .host = this->_brokerHost.c_str(),
        .port = this->_brokerPort,
        .buffer_size = 2560,
    };

    this->_client = esp_mqtt_client_init(&mqtt_cfg);

    if (this->_client == nullptr) {
        logger.log(INFO, "Failed to init MQTT client");
        return;
    }

    esp_mqtt_client_register_event(this->_client, MQTT_EVENT_ANY, this->_event_handler, this);
    esp_mqtt_client_start(this->_client);

    while (!this->_connected)
    {
        delay(100);
    }
}

void MqttClient::publish(const char *topic, void *data, size_t size)
{
    if (!this->_client) {
        logger.log(INFO, "MQTT connection not ready.");
        return;
    }

    int msg_id = esp_mqtt_client_publish(this->_client, topic, (const char *)data, size, 0, 0);
    if (msg_id < 0)
    {
        logger.log(INFO, "Failed to publish message.");
    }
}

void MqttClient::subscribe(const char *topic, MESSAGE_HANDLER handler)
{
    int msg_id = esp_mqtt_client_subscribe(this->_client, topic, 0);

    if (msg_id < 0)
    {
        logger.log(INFO, "Failed to subscribe topic: %s. rc: %d", topic, msg_id);
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