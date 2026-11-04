/**
 * @file config.h
 * @brief Global system configuration and inter-task communication definitions.
 *
 * This module centralizes:
 * - FreeRTOS queue handles
 * - System-wide command payloads
 * - Inter-task message formats
 *
 * It acts as the communication contract between UI, Motor, Power,
 * Encoder, and Settings persistence subsystems.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>

/**
 * @brief UI input event queue (encoder + buttons).
 */
extern QueueHandle_t xUIQueue;

/**
 * @brief Motor control command queue.
 */
extern QueueHandle_t xMotorQueue;

/**
 * @brief Power management command queue.
 */
extern QueueHandle_t xPowerQueue;

/**
 * @brief Persistent settings command queue.
 */
extern QueueHandle_t xSettingsQueue;

/**
 * @brief Buzzer command queue.
 */
extern QueueHandle_t xBuzzerQueue;

/* =========================
   UI EVENTS
   ========================= */

/**
 * @brief Rotary encoder and button event types.
 *
 * Used by TaskEncoder to communicate UI interactions
 * to the UI task and state machine.
 */
typedef enum
{
    ENC_LEFT   = 0, /**< Encoder rotated counter-clockwise */
    ENC_RIGHT  = 1, /**< Encoder rotated clockwise */
    BTN_SHORT  = 2, /**< Short button press */
    BTN_LONG   = 3  /**< Long button press */
}EncoderEvent;

/* =========================
   BUZZER COMMANDS
   ========================= */

/**
 * @brief Buzzer command types. Order must match `MELODIES[]` in TaskBuzzer.h.
 */
typedef enum
{
    BUZZER_CMD_INIT,           /**< System startup melody */
    BUZZER_CMD_CONFIRM,        /**< User confirmation feedback */
    BUZZER_CMD_ERROR,          /**< Error indication */
    BUZZER_CMD_CYCLE_FINISHED, /**< Cycle completion notification */
    BUZZER_CMD_COUNT           /**< Sentinel — number of valid commands */
}BuzzerCmdType;

/**
 * @brief Buzzer command payload.
 *
 * Sent from UI and system tasks to TaskBuzzer.
 */
typedef struct
{
    BuzzerCmdType type; /**< Melody to play */
}BuzzerCommand;

/* =========================
   MOTOR COMMANDS
   ========================= */

/**
 * @brief Motor control command identifiers.
 */
typedef enum
{
    MOTOR_CMD_SET_SPEED,     /**< Set PWM speed only */
    MOTOR_CMD_START_TIMED,   /**< Start motor with timeout */
    MOTOR_CMD_STOP,          /**< Stop motor immediately */
    MOTOR_CMD_CLEAN_FAST,    /**< Execute fast cleaning routine */
    MOTOR_CMD_CLEAN_SLOW,    /**< Execute slow cleaning routine */
    MOTOR_CMD_CLEAN_MANUAL,  /**< Execute manual cleaning routine */
    MOTOR_CMD_CLEAN_PURGE,   /**< Execute purge cleaning routine */

    /* Internal commands — posted by timer callbacks to delegate state changes
     * to TaskMotor. Do not send these from any other task. */
    MOTOR_CMD_INTERNAL_KICKSTART, /**< Apply kickstart target duty after boost phase */
    MOTOR_CMD_INTERNAL_TIMEOUT,   /**< End timed operation and notify completion */
    MOTOR_CMD_INTERNAL_CYCLE,     /**< Advance cleaning sequence ON/OFF phase */
    MOTOR_CMD_INTERNAL_DRIP,      /**< Advance drip burst ON/OFF phase */
}MotorCmdType;

/**
 * @brief Motor command message payload.
 *
 * Sent from UI and system tasks to TaskMotor.
 */
typedef struct
{
    MotorCmdType type; /**< Command type */
    uint8_t speed;     /**< Speed percentage (0–100) */
    uint32_t duration;/**< Run duration in ms */
}MotorCommand;

/* =========================
   POWER COMMANDS
   ========================= */

/**
 * @brief Power management command identifiers.
 */
typedef enum
{
    POWER_CMD_SHUTDOWN, /**< Enter deep sleep */
}PowerCmdType;

/**
 * @brief Power command payload.
 */
typedef struct
{
    PowerCmdType type; /**< Power command selector */
}PowerCommand;

/* =========================
   SETTINGS COMMANDS
   ========================= */

/**
 * @brief Persistent configuration payload structure.
 *
 * Contains user-adjustable parameters that must survive reboot.
 */
typedef struct
{
    uint8_t motorSpeed; /**< Last motor speed setting */
    uint8_t timeIndex;  /**< Last time selection index */
}SettingsPayload;

/**
 * @brief Settings task command identifiers.
 */
typedef enum
{
    SETTINGS_CMD_SAVE, /**< Save current configuration */
    SETTINGS_CMD_LOAD  /**< Load stored configuration */
}SettingsCmdType;

/**
 * @brief Settings command message wrapper.
 */
typedef struct
{
    SettingsCmdType type; /**< Command selector */
    SettingsPayload data; /**< Associated configuration data */
}SettingsCommand;

/* =========================
   INITIALIZATION
   ========================= */

/**
 * @brief Initializes all system queues.
 *
 * Must be called once during system startup
 * before any task attempts queue communication.
 */
void Config_init();

/**
 * @brief Posts a motor command to `xMotorQueue`.
 *
 * @param type     Command type.
 * @param speed    Speed percentage (0–100); used by MOTOR_CMD_SET_SPEED and
 *                 MOTOR_CMD_START_TIMED; ignored for all other command types.
 * @param duration Run duration in ms; 0 means no timeout.
 */
bool sendMotorRequest(MotorCmdType type, int speed, uint32_t duration);

/**
 * @brief Posts a power command to `xPowerQueue`.
 *
 * @param type Command type.
 */
void sendPowerRequest(PowerCmdType type);

/**
 * @brief Posts a settings command to `xSettingsQueue`.
 *
 * For `SETTINGS_CMD_SAVE`, `motorSpeed` and `timeIndex` are stored to flash.
 * For `SETTINGS_CMD_LOAD`, those parameters are ignored — TaskSaveData reads
 * from flash directly.
 *
 * @param type       Command type.
 * @param motorSpeed Speed value to save (0–100).
 * @param timeIndex  Time option index to save.
 */
void sendSettingsSave(SettingsCmdType type, int motorSpeed, uint8_t timeIndex);

/**
 * @brief Posts a buzzer command to `xBuzzerQueue`.
 *
 * @param type Command type.
 */
bool sendBuzzerCommand(BuzzerCmdType type);

#endif
