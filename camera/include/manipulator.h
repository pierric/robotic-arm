#ifndef _MANIPULATOR_H
#define _MANIPULATOR_H

#include <esp_err.h>

esp_err_t initManiplator(void);

void onManipulatorCommand(void *, size_t, size_t, size_t);

#endif