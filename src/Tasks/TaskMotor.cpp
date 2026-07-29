/**
 * @file TaskMotor.cpp
 * @brief Motor control task implementation.
 *
 * Implements two independent operating modes:
 * - **Drip mode** (`MOTOR_CMD_START_TIMED`): short high-amplitude PWM bursts
 *   timed by a dedicated FreeRTOS timer. Speed 1–100 maps hyperbolically to
 *   a burst period of 10 000–222 ms, covering the functional drip range of
 *   the physical hardware (0–45 % internal duty).
 * - **Cleaning mode** (`MOTOR_CMD_CLEAN_*`): continuous high-speed PWM driven
 *   by cycling ON/OFF according to a `CleanProfile`.
 */

#include "Tasks/TaskMotor.h"

/** @brief Duration of a full purge cycle in milliseconds. */
static constexpr uint32_t PURGE_DURATION_MS = 20000;

/** @brief Default burst amplitude for drip mode. */
static constexpr uint8_t DRIP_PULSE_DUTY_DEFAULT = 70;

/**
 * @brief Default burst duration in milliseconds for drip mode.
 *
 * Determines how long the motor runs per pulse and therefore the fluid volume
 * per drop.
 */
static constexpr uint32_t DRIP_PULSE_MS_DEFAULT = 10;

/**
 * @brief One-shot timer that fires after KICKSTART_MS to drop PWM to the
 *        target duty, ending the kickstart phase.
 */
static TimerHandle_t kickstartTimer = nullptr;

/** @brief Target LEDC duty to apply when the kickstart timer fires. */
static uint32_t kickstartTargetDuty = 0;

/** @brief Tracks whether the motor is currently spinning. */
static bool motorRunning = false;

/** @brief One-shot timer that stops the motor after a timed operation ends. */
static TimerHandle_t motorTimeoutTimer = nullptr;

/** @brief One-shot timer that drives ON/OFF cycles during cleaning sequences. */
static TimerHandle_t motorCycleTimer = nullptr;

/** @brief One-shot timer that alternates burst ON/OFF phases in drip mode. */
static TimerHandle_t dripTimer = nullptr;

/** @brief Cleaning sequence configuration profile. */
struct CleanProfile
{
    uint32_t onTimeMs;   ///< Motor ON duration (ms)
    uint32_t offTimeMs;  ///< Motor OFF duration (ms)
    uint8_t  speed;      ///< Motor speed (0-100%)
    uint8_t  cycles;     ///< Number of ON/OFF cycles
};

/**
 * @brief Cleaning profiles indexed by (mode - MOTOR_CMD_CLEAN_FAST).
 *
 * [0] CLEAN_FAST · [1] CLEAN_SLOW · [2] CLEAN_MANUAL.
 * Must stay in sync with the MotorCmdType enum order in config.h.
 */
static const CleanProfile cleanProfiles[] = {
    { 5000,  1000, 100, 10 },  // MOTOR_CMD_CLEAN_FAST
    { 20000, 2000, 60,  10 },  // MOTOR_CMD_CLEAN_SLOW
    { 2000,  500,  100, 15 },  // MOTOR_CMD_CLEAN_MANUAL
};

/** @brief Maps a clean command type to its CleanProfile entry. */
#define GET_CLEAN_PROFILE(mode) (&cleanProfiles[(mode) - MOTOR_CMD_CLEAN_FAST])

/** @brief Runtime state for an active cleaning sequence. */
struct CleanState {
    MotorCmdType mode;       ///< Active cleaning command; MOTOR_CMD_STOP when idle.
    uint8_t      cyclesLeft; ///< Remaining ON/OFF cycles before completion.
    bool         isMotorOn;  ///< Whether the motor is currently in its ON phase.
};

static CleanState cleanState = {
    .mode       = MOTOR_CMD_STOP,
    .cyclesLeft = 0,
    .isMotorOn  = false
};

/**
 * @brief Runtime state for an active drip operation.
 *
 * The drip timer alternates between two phases:
 *  - ON  (`motorPhase == true`):  motor runs for `pulseMs` milliseconds.
 *  - OFF (`motorPhase == false`): motor is idle for `periodMs - pulseMs` ms.
 */
struct DripState {
    bool     active;           ///< True while a drip operation is running.
    uint32_t periodMs;         ///< Full ON+OFF cycle duration in milliseconds.
    uint32_t pulseMs;          ///< Motor ON duration per cycle (burst width).
    uint8_t  pulseDutyPercent; ///< Burst amplitude as a linear duty percentage.
    bool     motorPhase;       ///< True during the ON phase, false during OFF.
};

static DripState dripState = {
    .active           = false,
    .periodMs         = 0,
    .pulseMs          = 0,
    .pulseDutyPercent = 0,
    .motorPhase       = false
};

/**
 * @brief Maps a speed percentage to an LEDC duty value using a quadratic curve.
 *
 * @param percent Speed in the range 0–100 %.
 * @return LEDC duty value in the range 0–MAX_DUTY.
 */
static uint32_t speedToDuty(uint8_t percent)
{
    if (percent == 0)
        return 0;

    const float minDuty = 0.20f;

    float x = percent / 100.0f;
    float y = minDuty + (1.0f - minDuty) * x * x;

    return (uint32_t)(y * MAX_DUTY);
}

/**
 * @brief Writes a linear duty value directly to the LEDC hardware.
 *
 * @param percent Duty as a linear percentage (0–100). Values are scaled
 *                directly: `duty = percent × MAX_DUTY / 100`.
 */
static void Drip_applyPulse(uint8_t percent)
{
    uint32_t duty = (percent == 0) ? 0 : (uint32_t)(percent * MAX_DUTY / 100UL);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL);
}

/** @brief Delegates kickstart completion to TaskMotor via queue. */
static void kickstartCallback(TimerHandle_t)
{
    sendMotorRequest(MOTOR_CMD_INTERNAL_KICKSTART, 0, 0);
}

/**
 * @brief Sets the motor speed using continuous PWM.
 *
 * When starting from rest with a target duty below 50 % of MAX_DUTY, a
 * kickstart pulse at 70 % duty is applied first for KICKSTART_MS milliseconds
 * to overcome static friction. The `kickstartTimer` callback then drops the
 * output to the true target.
 *
 * Calling with percent ≤ 0 stops the motor immediately, cancels any pending
 * kickstart, and resets `motorRunning`.
 *
 * @param percent Target speed in the range 0–100 %. Clamped internally.
 */
static void Motor_setSpeed(int percent)
{
    if (percent <= 0)
    {
        motorRunning = false;

        if (kickstartTimer)
            xTimerStop(kickstartTimer, 0);

        ledc_set_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL);
        return;
    }

    if (percent > 100)
        percent = 100;

    kickstartTargetDuty = speedToDuty(percent);

    if (!motorRunning)
    {
        motorRunning = true;

        if (kickstartTargetDuty < (MAX_DUTY / 2))
        {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL, (uint32_t)(0.7f * MAX_DUTY));
            ledc_update_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL);

            xTimerStop(kickstartTimer, 0);
            xTimerStart(kickstartTimer, 0);
        }
        else
        {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL, kickstartTargetDuty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL);
        }
    }
    else
    {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL, kickstartTargetDuty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL);
    }
}

/** @brief Delegates drip burst phase advance to TaskMotor via queue. */
static void dripTimerCallback(TimerHandle_t)
{
    sendMotorRequest(MOTOR_CMD_INTERNAL_DRIP, 0, 0);
}

/**
 * @brief Starts the drip burst generator.
 *
 * Applies the first pulse immediately, then hands control to `dripTimer`
 * which alternates ON/OFF phases until stopDripMode() is called.
 *
 * @param periodMs         Full ON+OFF cycle duration in milliseconds.
 * @param pulseDutyPercent Burst amplitude as a linear duty percentage (0–100).
 * @param pulseMs          Burst ON duration in milliseconds.
 */
static void startDripMode(uint32_t periodMs, uint8_t  pulseDutyPercent = DRIP_PULSE_DUTY_DEFAULT, uint32_t pulseMs = DRIP_PULSE_MS_DEFAULT)
{
    if (periodMs < 50) periodMs = 50;

    dripState.active           = true;
    dripState.periodMs         = periodMs;
    dripState.pulseDutyPercent = pulseDutyPercent;
    dripState.pulseMs          = pulseMs;
    dripState.motorPhase       = true;

    Drip_applyPulse(pulseDutyPercent);

    xTimerChangePeriod(dripTimer, pdMS_TO_TICKS(pulseMs), 0);
}

/**
 * @brief Stops the drip burst generator and ensures the motor output is zero.
 */
static void stopDripMode()
{
    if (dripState.active)
    {
        dripState.active = false;
        xTimerStop(dripTimer, 0);
        Drip_applyPulse(0);
    }
}

/* =========================
   STOP & NOTIFY
   ========================= */

/**
 * @brief Stops all motor operations and resets all associated state.
 *
 * Cancels `motorTimeoutTimer`, `motorCycleTimer`, and the drip generator,
 * then stops the motor and clears the cleaning sequence state.
 */
static void stopAllMotorOperations()
{
    stopDripMode();

    xTimerStop(motorTimeoutTimer, 0);
    xTimerStop(motorCycleTimer, 0);
    Motor_setSpeed(0);

    cleanState.mode       = MOTOR_CMD_STOP;
    cleanState.cyclesLeft = 0;
    cleanState.isMotorOn  = false;
}

/**
 * @brief Signals cycle completion to the buzzer task.
 */
static void notifyCompletion()
{
    sendBuzzerCommand(BUZZER_CMD_CYCLE_FINISHED);
}

/* =========================
   TIMER CALLBACKS
   ========================= */

/** @brief Delegates timed-operation end to TaskMotor via queue. */
static void motorTimeoutCallback(TimerHandle_t)
{
    sendMotorRequest(MOTOR_CMD_INTERNAL_TIMEOUT, 0, 0);
}

/** @brief Delegates cleaning cycle phase advance to TaskMotor via queue. */
static void motorCycleCallback(TimerHandle_t)
{
    sendMotorRequest(MOTOR_CMD_INTERNAL_CYCLE, 0, 0);
}

/* =========================
   SEQUENCE CONTROL
   ========================= */

/**
 * @brief Starts a cleaning sequence for the given mode.
 *
 * Looks up the CleanProfile for `mode`, stops any active drip operation,
 * and begins the first ON phase. `motorCycleTimer` then drives subsequent
 * ON/OFF transitions until all cycles complete.
 *
 * @param mode One of MOTOR_CMD_CLEAN_FAST, MOTOR_CMD_CLEAN_SLOW, or
 *             MOTOR_CMD_CLEAN_MANUAL.
 */
static void startCleaningSequence(MotorCmdType mode)
{
    if (mode < MOTOR_CMD_CLEAN_FAST || mode > MOTOR_CMD_CLEAN_MANUAL)
        return;

    const CleanProfile* profile = GET_CLEAN_PROFILE(mode);

    if (profile->cycles == 0)
        return;

    stopDripMode();

    cleanState.mode       = mode;
    cleanState.cyclesLeft = profile->cycles;
    cleanState.isMotorOn  = true;

    Motor_setSpeed(profile->speed);

    xTimerChangePeriod(motorCycleTimer, pdMS_TO_TICKS(profile->onTimeMs), 0);
}

/**
 * @brief Starts a drip operation for a given speed and duration.
 *
 * Maps `speed` (1–100 %) to the functional hardware range (1–45 %)
 * then converts to a burst period
 *
 * If `durationMs` is non-zero the operation is automatically stopped by
 * `motorTimeoutTimer` after that interval.
 *
 * @param speed      Requested speed (0–100 %). 0 stops the motor.
 * @param durationMs Run duration in milliseconds. 0 means indefinite.
 */
static void startTimedOperation(uint8_t speed, uint32_t durationMs)
{
    stopDripMode();

    if (speed > 0)
    {
        uint8_t  mapped   = (uint8_t)((speed * 45UL + 99UL) / 100UL);
        uint32_t periodMs = 10000UL / mapped;
        startDripMode(periodMs);
    }

    if (durationMs > 0)
    {
        xTimerChangePeriod(motorTimeoutTimer, pdMS_TO_TICKS(durationMs), 0);
    }
}

/**
 * @brief Runs the motor at full speed for PURGE_DURATION_MS milliseconds.
*/
static void startPurgeMode()
{
    stopDripMode();

    Motor_setSpeed(100);

    xTimerChangePeriod(motorTimeoutTimer, pdMS_TO_TICKS(PURGE_DURATION_MS), 0);
}

void TaskMotor(void* pvParameters)
{
    MotorCommand cmd;

    for (;;)
    {
        if (xQueueReceive(xMotorQueue, &cmd, portMAX_DELAY) == pdTRUE)
        {
            switch (cmd.type)
            {
                case MOTOR_CMD_SET_SPEED:
                    stopDripMode();
                    Motor_setSpeed(cmd.speed);
                    break;

                case MOTOR_CMD_START_TIMED:
                    startTimedOperation(cmd.speed, cmd.duration);
                    break;

                case MOTOR_CMD_STOP:
                    stopAllMotorOperations();
                    break;

                case MOTOR_CMD_CLEAN_FAST:
                case MOTOR_CMD_CLEAN_SLOW:
                case MOTOR_CMD_CLEAN_MANUAL:
                    startCleaningSequence(cmd.type);
                    break;

                case MOTOR_CMD_CLEAN_PURGE:
                    startPurgeMode();
                    break;

                case MOTOR_CMD_INTERNAL_KICKSTART:
                    if (motorRunning)
                    {
                        ledc_set_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL, kickstartTargetDuty);
                        ledc_update_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL);
                    }
                    break;

                case MOTOR_CMD_INTERNAL_TIMEOUT:
                    stopAllMotorOperations();
                    notifyCompletion();
                    break;

                case MOTOR_CMD_INTERNAL_CYCLE:
                {
                    if (cleanState.mode < MOTOR_CMD_CLEAN_FAST || cleanState.mode > MOTOR_CMD_CLEAN_MANUAL)
                        break;

                    const CleanProfile* profile = GET_CLEAN_PROFILE(cleanState.mode);

                    if (cleanState.isMotorOn)
                    {
                        Motor_setSpeed(0);
                        cleanState.isMotorOn = false;

                        if (cleanState.cyclesLeft > 0)
                            cleanState.cyclesLeft--;

                        if (cleanState.cyclesLeft == 0)
                        {
                            cleanState.mode = MOTOR_CMD_STOP;
                            notifyCompletion();
                            break;
                        }
                        xTimerChangePeriod(motorCycleTimer, pdMS_TO_TICKS(profile->offTimeMs), 0);
                    }
                    else
                    {
                        Motor_setSpeed(profile->speed);
                        cleanState.isMotorOn = true;
                        xTimerChangePeriod(motorCycleTimer, pdMS_TO_TICKS(profile->onTimeMs), 0);
                    }
                    break;
                }

                case MOTOR_CMD_INTERNAL_DRIP:
                {
                    if (!dripState.active) break;

                    if (dripState.motorPhase)
                    {
                        Drip_applyPulse(0);
                        dripState.motorPhase = false;

                        uint32_t offTimeMs = (dripState.periodMs > dripState.pulseMs)
                                           ? (dripState.periodMs - dripState.pulseMs) : 10;
                        xTimerChangePeriod(dripTimer, pdMS_TO_TICKS(offTimeMs), 0);
                    }
                    else
                    {
                        Drip_applyPulse(dripState.pulseDutyPercent);
                        dripState.motorPhase = true;
                        xTimerChangePeriod(dripTimer, pdMS_TO_TICKS(dripState.pulseMs), 0);
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }
}

void TaskMotor_init()
{
    ledc_timer_config_t timer_config = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = MOTOR_PWM_TIMER,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t channel_config = {
        .gpio_num   = PIN_MOTOR,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = MOTOR_PWM_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = MOTOR_PWM_TIMER,
        .duty       = 0,
        .hpoint     = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

    motorTimeoutTimer = xTimerCreate(
        "MotorTimeout",
        pdMS_TO_TICKS(1000),
        pdFALSE,
        nullptr,
        motorTimeoutCallback
    );
    configASSERT(motorTimeoutTimer);

    motorCycleTimer = xTimerCreate(
        "MotorCycle",
        pdMS_TO_TICKS(1000),
        pdFALSE,
        nullptr,
        motorCycleCallback
    );
    configASSERT(motorCycleTimer);

    kickstartTimer = xTimerCreate(
        "Kickstart",
        pdMS_TO_TICKS(KICKSTART_MS),
        pdFALSE,
        nullptr,
        kickstartCallback
    );
    configASSERT(kickstartTimer);

    dripTimer = xTimerCreate(
        "DripTimer",
        pdMS_TO_TICKS(100),
        pdFALSE,
        nullptr,
        dripTimerCallback
    );
    configASSERT(dripTimer);

    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        TaskMotor,
        "TaskMotor",
        4096,
        nullptr,
        1,
        nullptr,
        APP_CPU_NUM
    );
    configASSERT(taskCreated == pdPASS);
}