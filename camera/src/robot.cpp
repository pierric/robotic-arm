#include <vector>
#include <optional>
#include <sstream>
#include <iomanip>
#include <string>
#include <Elog.h>
#include <esp_err.h>
#include "driver/twai.h"

#include "robot.h"

using std::vector;
using std::optional;

extern Elog logger;
extern float getManipulatorState(void);

void initCan()
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_14, GPIO_NUM_15, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_25KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        logger.log(INFO, "CanBus driver installed");
    } else {
        logger.log(INFO, "Failed to install CanBus driver");
        return;
    }

    if (twai_start() == ESP_OK) {
        logger.log(INFO, "CanBus driver started");
    } else {
        logger.log(INFO, "Failed to start CanBus driver");
        return;
    }        
}

optional<vector<uint8_t>> read_status()
{
    esp_err_t rc;
    int cnt = 0;
    twai_message_t message;
    vector<uint8_t> full_payload;

    do {
        cnt += 1;
        rc = twai_receive(&message, 50);
        if (rc == ESP_ERR_TIMEOUT) continue;
        if (rc != ESP_OK) {
            logger.log(INFO, "Failed to read CanBus message: %s", esp_err_to_name(rc));
            break;
        }
        if (message.identifier == 0x301){
            std::copy(message.data, message.data + message.data_length_code, std::back_insert_iterator(full_payload));
        }
        else if (message.identifier == 0x302) {
            return full_payload;
        }
    } while (cnt < 20);

    logger.log(INFO, "Failed to make a full payload. Error in receiving or no sentry message.");
    return {};
}

optional<struct RobotStatus> query_status()
{
    esp_err_t rc;
    twai_message_t message = {.identifier = 0x300, .data_length_code = 0};

    if (twai_transmit(&message, pdMS_TO_TICKS(10)) != ESP_OK) {
        logger.log(INFO, "Failed to queue the CanBus message");
        return {};
    }

    if (auto status = read_status()) {
        std::ostringstream oss;
        for (auto it=status->begin(); it!=status->end(); ++it) {
            oss << std::setw(2) << std::setfill('0') << std::hex << int(*it) << " ";
        }
        logger.log(DEBUG, "Full message: [%d] %s", status->size(), oss.str().c_str());

        uint8_t *buf = status->data();
        float* positions = reinterpret_cast<float *>(buf + 4);
        size_t num_positions = (status->size() - 4) / 4;
        auto positions_vec = vector<float>();
        std::copy(positions, positions + num_positions, std::back_insert_iterator(positions_vec));
        return RobotStatus {.homed = bool(buf[0]), .positions = positions_vec, .gripper_position = getManipulatorState()};
    }

    return {};
}

std::string dump_status(const struct RobotStatus& status)
{
    std::ostringstream oss;

    oss << "homed? " << status.homed << " positions: ";

    for (auto pos: status.positions) {
        oss << std::setw(4) << std::setprecision(2) << pos << " ";
    }

    oss << std::setw(4) << std::setprecision(2) << status.gripper_position;

    return oss.str();
}