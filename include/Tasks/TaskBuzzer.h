/**
 * @file TaskBuzzer.h
 * @brief PWM-based buzzer driver using ESP32 LEDC and FreeRTOS.
 *
 * Commands posted to `xBuzzerQueue` are played in order; a melody in
 * progress cannot be interrupted.
 */
#ifndef TASKBUZZER_H
#define TASKBUZZER_H

#include "Config/config.h"
#include "Config/pins.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/ledc.h>
#include <Arduino.h>

/* =========================
   HARDWARE CONFIGURATION
   ========================= */

/** @brief LEDC channel used for tone generation. */
#define BUZZER_PWM_CHANNEL  LEDC_CHANNEL_1

/** @brief LEDC timer used for PWM generation. */
#define BUZZER_PWM_TIMER    LEDC_TIMER_1

/** @brief 10-bit PWM resolution (duty range 0–1023). */
#define BUZZER_RESOLUTION   LEDC_TIMER_10_BIT

/**
 * @brief Duty cycle for tone output (≈ 10 %).
 *
 * Below 50 % to keep volume non-intrusive; passive buzzers are loudest at 50 %.
 */
#define BUZZER_DUTY_CYCLE   102


/* =========================
   MUSICAL NOTES (Hz)
   ========================= */

/** @brief Silent pause marker. Stops the channel for the note duration. */
#define NOTE_SILENT 0

/** @brief Musical note frequencies (Hz), C5 through G6. */
#define NOTE_C5   523
#define NOTE_D5   587
#define NOTE_E5   659
#define NOTE_F5   698
#define NOTE_G5   784
#define NOTE_A5   880
#define NOTE_B5   988
#define NOTE_C6   1047
#define NOTE_D6   1175
#define NOTE_E6   1319
#define NOTE_F6   1397
#define NOTE_G6   1568


/* =========================
   DATA STRUCTURES
   ========================= */

/** @brief A single musical note. */
struct Note {
    uint16_t frequency;  /**< Frequency in Hz; 0 for a silent pause. */
    uint16_t duration;   /**< Duration in milliseconds. */
};

/** @brief An ordered sequence of notes. */
struct Melody {
    const Note* notes;   /**< Note array. */
    uint8_t     length;  /**< Number of notes. */
};

/* =========================
   PREDEFINED MELODIES
   ========================= */

/** @brief System initialization melody. */
static const Note MELODY_INIT[] = {
    {NOTE_C5, 120},
    {NOTE_E5, 120},
    {NOTE_G5, 150},
    {NOTE_C6, 220},

    {NOTE_SILENT, 80},

    {NOTE_G5, 140},
    {NOTE_A5, 160},
    {NOTE_C6, 220},

    {NOTE_SILENT, 60},

    {NOTE_E6, 350}
};

/** @brief Confirmation melody. */
static const Note MELODY_CONFIRM[] = {
    {NOTE_E5, 100},
    {NOTE_SILENT, 50},
    {NOTE_G5, 100}
};

/** @brief Error melody. */
static const Note MELODY_ERROR[] = {
    {NOTE_C5, 80},
    {NOTE_SILENT, 40},
    {NOTE_C5, 80},
    {NOTE_SILENT, 40},
    {NOTE_C5, 80}
};

/** @brief Cycle completion melody. */
static const Note MELODY_CYCLE_FINISHED[] = {

    {NOTE_C5, 300},
    {NOTE_E5, 300},
    {NOTE_G5, 400},
    {NOTE_SILENT, 200},

    {NOTE_E5, 250},
    {NOTE_G5, 250},
    {NOTE_C6, 400},
    {NOTE_D6, 300},
    {NOTE_E6, 600},
    {NOTE_SILENT, 200},

    {NOTE_G5, 250},
    {NOTE_A5, 250},
    {NOTE_C6, 350},
    {NOTE_E6, 350},
    {NOTE_D6, 350},
    {NOTE_C6, 600},
    {NOTE_SILENT, 250},
};

/**
 * @brief Lookup table indexed directly by `BuzzerCmdType` value.
 *
 * Must stay in sync with the `BuzzerCmdType` enum order in config.h.
 */
static const Melody MELODIES[] = {
    {MELODY_INIT,           sizeof(MELODY_INIT)           / sizeof(Note)},
    {MELODY_CONFIRM,        sizeof(MELODY_CONFIRM)        / sizeof(Note)},
    {MELODY_ERROR,          sizeof(MELODY_ERROR)          / sizeof(Note)},
    {MELODY_CYCLE_FINISHED, sizeof(MELODY_CYCLE_FINISHED) / sizeof(Note)}
};


/* =========================
   PUBLIC API
   ========================= */

/**
 * @brief Initializes LEDC hardware and creates the playback task.
 *
 * Must be called once during system init, after `Config_init()`.
 */
void TaskBuzzer_init();

/**
 * @brief FreeRTOS task that serialises melody playback.
 *
 * Blocks on `xBuzzerQueue`. Plays each melody to completion before
 * accepting the next command.
 *
 * @param pvParameters Unused.
 */
void TaskBuzzer(void *pvParameters);

/**
 * @brief Produces a single tone at the given frequency.
 *
 * If `frequency` is `NOTE_SILENT`, calls `Buzzer_stop()` instead.
 *
 * @param frequency Frequency in Hz, or `NOTE_SILENT` for silence.
 */
void Buzzer_playTone(uint16_t frequency);

/**
 * @brief Stops buzzer output.
 *
 * Uses `ledc_stop()` to drive the GPIO to its idle level (low).
 */
void Buzzer_stop();

/**
 * @brief Plays a melody sequence, blocking until complete.
 *
 * Blocks the calling task for the full melody duration. Must only be
 * called from within `TaskBuzzer`.
 *
 * @param melody Pointer to the melody to play.
 */
void Buzzer_playMelody(const Melody* melody);

#endif // TASKBUZZER_H