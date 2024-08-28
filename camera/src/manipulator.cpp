#include <stddef.h>
#include <cstring>
#include <algorithm>
#include <Elog.h>
#include <Servo.h>

extern Elog logger;
Servo servo;

const int servoPin = 12;

void initServo()
{
    servo.attach(servoPin, Servo::CHANNEL_NOT_ATTACHED, 0, 90 );
    logger.log(INFO, "servo: attached.");
    servo.write(0);
}

void onManipulatorCommand(void *message, size_t size, size_t offset, size_t total)
{
    char buffer[64];
    size_t len = std::min(size, (size_t) 63);
    strncpy(buffer, (const char *)message, len);
    buffer[len] = 0;
    logger.log(INFO, "mqtt, receiving command: %s", buffer);

    float angle = std::max(0.f, std::min(180.f, std::stof(buffer)));
    logger.log(INFO, "mqtt, setting angle: %f", angle);

    servo.write(angle);
}

float getManipulatorState(void)
{
    return (float)servo.read();
}