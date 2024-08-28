#ifndef _MQTT_H
#define _MQTT_H

#include <string>
#include <unordered_map>
#include <mqtt_client.h>

typedef void (*MESSAGE_HANDLER)(void *message, size_t size, size_t offset, size_t total);

class MqttClient {
public:
    MqttClient(const std::string& broker_endpoint);
    void connect();

    void publish(const char *topic, void *data, size_t size);

    void subscribe(const char *topic, MESSAGE_HANDLER MESSAGE_HANDLER);

    MESSAGE_HANDLER find_message_handler(const std::string &topic);

    bool connected(void) { return _connected; }

private:
    esp_mqtt_client_handle_t _client {nullptr};
    bool _connected {false};

    std::string _brokerHost;
    uint32_t _brokerPort;
    std::string _messageTopic;
    std::unordered_map<std::string, MESSAGE_HANDLER> _handlers;

    static void _event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
};

#endif