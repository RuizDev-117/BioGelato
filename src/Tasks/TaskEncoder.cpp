/**
 * @file TaskEncoder.cpp
 * @brief Rotary encoder and push-button task manager.
 *
 * This module reads a rotary encoder and its push-button,
 * decodes quadrature rotation, performs button debouncing and
 * long-press detection, and sends UI events to the UI queue.
 */
#include "Tasks/TaskEncoder.h"
#include "Config/pins.h"

/**
 * @brief Previous sampled 2-bit encoder state: (PIN_CLK << 1) | PIN_DT.
 *
 * Both phases must be tracked together to decode EC11 quadrature output.
 * Seeded from a real pin read at task start and updated on every poll.
 */
static uint8_t lastEncoderState = 0;

/**
 * @brief Signed accumulator of quadrature edges since the last detent.
 *
 * Each valid edge adds +1 (CW) or -1 (CCW); a UI event fires and the
 * accumulator unwinds by STEPS_PER_DETENT once a full detent is reached.
 */
static int8_t encoderAccumulator = 0;

/**
 * @brief Last confirmed button state.
 */
static bool lastButtonState = HIGH;

/**
 * @brief Last raw button reading and the time it last changed (ms).
 */
static bool     lastRawButtonState = HIGH;
static uint32_t buttonDebounceMs   = 0;

/**
 * @brief Timestamp when button was pressed (ms).
 */
static uint32_t buttonPressTime = 0;

/**
 * @brief Flag indicating that long press event has been fired.
 */
static bool longPressFired = false;

/**
 * @brief Button stable time required before a level change is accepted (ms).
 */
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 20;

/**
 * @brief Button long-press threshold in milliseconds.
 */
static constexpr uint32_t LONG_PRESS_MS = 1000;

/**
 * @brief Quadrature transitions accumulated per mechanical detent.
 *
 * The ALPS EC11 in this build has 18 pulses and 36 detents per revolution
 * (datasheet): 18 pulses x 4 quadrature edges = 72 edges / 36 detents = 2
 * edges per detent. One UI event is emitted per detent, i.e. each time the
 * signed accumulator reaches +/- this value. Set to 4 for EC11 variants
 * that complete a full Gray cycle (4 edges) per detent.
 */
static constexpr int8_t STEPS_PER_DETENT = 2;

/**
 * @brief Quadrature decode table indexed by (prev_state << 2) | current_state.
 *
 * Each state is the 2-bit pair (A << 1) | B sampled from PIN_CLK / PIN_DT.
 * The table maps a single state transition to a rotation direction:
 *   +1  valid clockwise step          (Gray-code neighbour in CW order)
 *   -1  valid counter-clockwise step
 *    0  invalid transition: state unchanged, or a double-bit jump caused
 *        by contact bounce or sliding noise — rejected inherently.
 *
 * Rows are grouped by previous state (00, 01, 10, 11); the four columns
 * within each row are the current state (00, 01, 10, 11). Only the four
 * single-bit Gray transitions per cycle score non-zero, which is why this
 * decoder needs no time-based debounce.
 */
static constexpr int8_t QUAD_TABLE[16] = {
    /* prev 00 -> */  0, -1, +1,  0,
    /* prev 01 -> */ +1,  0,  0, -1,
    /* prev 10 -> */ -1,  0,  0, +1,
    /* prev 11 -> */  0, +1, -1,  0
};

/**
 * @brief Decodes the EC11 rotary encoder via a quadrature transition table.
 *
 * Reads both phases every poll, builds the 2-bit state and looks up the
 * (previous, current) transition in QUAD_TABLE. Valid edges (+/-1) are
 * summed in a signed accumulator; a UI event fires each time the
 * accumulator completes one detent (STEPS_PER_DETENT edges), after which
 * the remainder is carried forward. Invalid transitions score 0, so bounce
 * and sliding noise are rejected without any time-based debounce.
 * prev_state is updated on every poll — even on invalid transitions — so a
 * missed or noisy edge cannot desynchronise the decoder.
 */
static void readEncoder()
{
    /* Build current 2-bit state from both encoder phases. */
    uint8_t A     = (uint8_t)digitalRead(PIN_CLK);  /* phase A: 0 = closed, 1 = open */
    uint8_t B     = (uint8_t)digitalRead(PIN_DT);   /* phase B: 0 = closed, 1 = open */
    uint8_t state = (A << 1) | B;

    /* Look up the prev->current transition, then advance prev_state always. */
    int8_t step      = QUAD_TABLE[(lastEncoderState << 2) | state];
    lastEncoderState = state;

    /* Repeated state or noise: nothing to accumulate. */
    if (step == 0)
    {
        return;
    }

    encoderAccumulator += step;

    /* Emit one event per completed detent, carrying any remainder. */
    if (encoderAccumulator >= STEPS_PER_DETENT)
    {
        encoderAccumulator -= STEPS_PER_DETENT;
        EncoderEvent evt = ENC_RIGHT;
        /* xUIQueue overflow is expected under fast spinning: drop, don't assert. */
        (void)xQueueSend(xUIQueue, &evt, 0);
    }
    else if (encoderAccumulator <= -STEPS_PER_DETENT)
    {
        encoderAccumulator += STEPS_PER_DETENT;
        EncoderEvent evt = ENC_LEFT;
        (void)xQueueSend(xUIQueue, &evt, 0);
    }
}

/**
 * @brief Reads button state and generates short/long press events.
 */
static void readButton()
{
    bool     raw = digitalRead(PIN_SW);
    uint32_t now = millis();

    /* Debounce: ignore the level until it has been stable long enough. */
    if (raw != lastRawButtonState)
    {
        lastRawButtonState = raw;
        buttonDebounceMs   = now;
        return;
    }
    if (now - buttonDebounceMs < BUTTON_DEBOUNCE_MS)
    {
        return;
    }

    bool currentState = raw;

    /* Falling edge: button pressed */
    if (lastButtonState == HIGH && currentState == LOW)
    {
        buttonPressTime = now;
        longPressFired  = false;
    }

    /* Button held: long press detection */
    if (currentState == LOW && !longPressFired)
    {
        if (now - buttonPressTime >= LONG_PRESS_MS)
        {
            EncoderEvent evt = BTN_LONG;
            (void)xQueueSend(xUIQueue, &evt, 0);
            longPressFired = true;
        }
    }

    /* Rising edge: button released */
    if (lastButtonState == LOW && currentState == HIGH)
    {
        if (!longPressFired)
        {
            EncoderEvent evt = BTN_SHORT;
            (void)xQueueSend(xUIQueue, &evt, 0);
        }
    }

    lastButtonState = currentState;
}

/**
 * @brief Encoder polling task.
 *
 * Samples encoder rotation and push-button state at 1 ms intervals.
 * The fast poll lets the quadrature table capture intermediate states
 * during the EC11's 3 ms chattering. Sends UI events to xUIQueue.
 *
 * @param pvParameters Unused.
 */
void TaskEncoder(void *pvParameters)
{
    /* Seed prev_state from the current pins; start the accumulator empty. */
    uint8_t A          = (uint8_t)digitalRead(PIN_CLK);
    uint8_t B          = (uint8_t)digitalRead(PIN_DT);
    lastEncoderState   = (A << 1) | B;
    encoderAccumulator = 0;
    lastButtonState    = digitalRead(PIN_SW);
    lastRawButtonState = lastButtonState;
    buttonDebounceMs   = millis();
    buttonPressTime    = millis();
    longPressFired     = false;

    for (;;)
    {
        readEncoder();
        readButton();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief Initializes encoder GPIOs and creates encoder task.
 *
 * Encoder pins use plain INPUT because the PCB provides hardware
 * pull-ups and RC filters. The push-button uses INPUT_PULLUP as
 * the EC11 switch is active-low with no external pull-up.
 */
void TaskEncoder_init()
{
    pinMode(PIN_CLK, INPUT);   /* External pull-up on PCB */
    pinMode(PIN_DT,  INPUT);   /* External pull-up on PCB */
    pinMode(PIN_SW,  INPUT_PULLUP);

    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        TaskEncoder,
        "TaskEncoder",
        4096,
        nullptr,
        1,
        nullptr,
        APP_CPU_NUM
    );
    configASSERT(taskCreated == pdPASS);
}