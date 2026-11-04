/**
 * @file Config.cpp
 * @brief Global system queue initialization.
 */
#include "Config/config.h"

/* =========================
   QUEUE DEFINITIONS
   ========================= */

QueueHandle_t xUIQueue       = nullptr;
QueueHandle_t xMotorQueue    = nullptr;
QueueHandle_t xPowerQueue    = nullptr;
QueueHandle_t xSettingsQueue = nullptr;
QueueHandle_t xBuzzerQueue   = nullptr;

/* =========================
   INITIALIZATION
   ========================= */

/**
 * @brief Creates all FreeRTOS queues used by the system.
 */
void Config_init()
{
   xUIQueue       = xQueueCreate(10, sizeof(EncoderEvent)); configASSERT(xUIQueue);
   xMotorQueue    = xQueueCreate(4, sizeof(MotorCommand)); configASSERT(xMotorQueue);
   xPowerQueue    = xQueueCreate(2, sizeof(PowerCommand)); configASSERT(xPowerQueue);
   xSettingsQueue = xQueueCreate(2, sizeof(SettingsCommand)); configASSERT(xSettingsQueue);
   xBuzzerQueue   = xQueueCreate(4, sizeof(BuzzerCommand)); configASSERT(xBuzzerQueue);
}

bool sendMotorRequest(MotorCmdType type, int speed, uint32_t duration)
{
    if (speed < 0)   speed = 0;
    if (speed > 100) speed = 100;

    MotorCommand cmd = {
        .type     = type,
        .speed    = (uint8_t)speed,
        .duration = duration
    };

    configASSERT(xMotorQueue != NULL);
    return xQueueSend(xMotorQueue, &cmd, 0) == pdPASS;
}

void sendPowerRequest(PowerCmdType type)
{
    PowerCommand cmd = {};
    cmd.type = type;
    configASSERT(xQueueSend(xPowerQueue, &cmd, 0) == pdPASS);
}

void sendSettingsSave(SettingsCmdType type, int motorSpeed, uint8_t timeIndex)
{
    if (motorSpeed < 0)   motorSpeed = 0;
    if (motorSpeed > 100) motorSpeed = 100;

    SettingsCommand cmd = {};
    cmd.type = type;
    cmd.data.motorSpeed = (uint8_t)motorSpeed;
    cmd.data.timeIndex  = timeIndex;
    configASSERT(xQueueSend(xSettingsQueue, &cmd, 0) == pdPASS);
}

bool sendBuzzerCommand(BuzzerCmdType type)
{
    BuzzerCommand cmd = { .type = type };
    configASSERT(xBuzzerQueue != NULL);
    return xQueueSend(xBuzzerQueue, &cmd, 0) == pdPASS;
}