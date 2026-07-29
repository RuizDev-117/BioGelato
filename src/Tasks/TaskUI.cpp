/**
 * @file TaskUI.cpp
 * @brief UI task implementation.
 */

#include "Tasks/TaskUI.h"

/**
 * @brief UI task main loop.
 *
 * Blocks on xUIQueue waiting for EncoderEvent messages and
 * dispatches them to the UI FSM.
 *
 * @param pvParameters Unused.
 */
void TaskUI(void *pvParameters)
{
    EncoderEvent evt;
    for (;;)
    {
        if (xQueueReceive(xUIQueue, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        if (evt == ENC_LEFT || evt == ENC_RIGHT)
        {
            /*
             * Drain all pending rotation events before rendering so that fast
             * spinning resolves to a single draw at the final position.
             * A button event encountered mid-drain is pushed back to the front
             * of the queue and processed on the next iteration.
             */
            int delta = (evt == ENC_RIGHT) ? 1 : -1;
            EncoderEvent next;
            while (xQueueReceive(xUIQueue, &next, 0) == pdTRUE)
            {
                if      (next == ENC_RIGHT) { delta++; }
                else if (next == ENC_LEFT)  { delta--; }
                else
                {
                    xQueueSendToFront(xUIQueue, &next, 0);
                    break;
                }
            }
            UI_processMovement(delta > 0 ? 1 : -1);
        }
        else
        {
            UI_processEvent(evt);
        }
    }
}

/**
 * @brief Initializes UI subsystem and creates UI task.
 */
void TaskUI_init()
{
    UI_init();
    UI_setState(MENU_INIT); /* Force initial state BEFORE task starts processing events */
    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        TaskUI,
        "TaskUI",
        4096,
        nullptr,
        1,
        nullptr,
        APP_CPU_NUM
    );
    configASSERT(taskCreated == pdPASS);
}