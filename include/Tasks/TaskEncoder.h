#ifndef TASK_ENCODER_H
#define TASK_ENCODER_H

#include "Config/config.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @file TaskEncoder.h
 * @brief Encoder management interface.
 */

void TaskEncoder(void *pvParameters);
void TaskEncoder_init();

#endif // TASK_ENCODER_H