/**
 * @file pins.h
 * @brief Hardware GPIO pin assignments.
 *
 * Single source of truth for all physical pin numbers.
 * No other file may define hardware GPIO constants.
 */
#ifndef PINS_H
#define PINS_H

/** @brief Encoder CLK signal (external pull-up on PCB). */
static constexpr int PIN_CLK     = 11;

/** @brief Encoder DT signal (external pull-up on PCB). */
static constexpr int PIN_DT      = 10;

/** @brief Encoder push-button, active-low (EXT0 deep-sleep wake source). */
static constexpr int PIN_SW      = 12;

/** @brief Motor PWM output. */
static constexpr int PIN_MOTOR   = 21;

/** @brief Passive buzzer PWM output. */
static constexpr int PIN_BUZZER  = 47;

/** @brief TFT backlight LED enable (active-HIGH: HIGH = on, LOW = off). */
static constexpr int PIN_TFT_LED = 2;

#endif // PINS_H
