#ifndef _ROBOT_H
#define _ROBOT_H

#include <vector>
#include <optional>
#include <string>

struct alignas(4) RobotStatus {
    bool homed;
    std::vector<float> positions;
    float gripper_position;
};

std::optional<struct RobotStatus> query_status(void);
std::string dump_status(const struct RobotStatus &);

#endif