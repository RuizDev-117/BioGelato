/**
 * @file UIState.h
 * @brief Declarations for the User Interface (UI) state machine.
 *
 * This module defines the Finite State Machine for the UI menu,
 * lookup tables for menu titles and icons, and utility functions
 * to manage state transitions and encoder events.
 */
#ifndef UISTATE_H
#define UISTATE_H

#include "UI/UI.h"
#include "Config/config.h"
#include "images/icons.h"

static constexpr uint8_t TIME_OPTION_COUNT  = 5;
static constexpr uint8_t TIME_DEFAULT_INDEX = TIME_OPTION_COUNT - 1;

/**
 * @enum UIState
 * @brief Defines the possible states of UI.
 */
#define UI_STATE_INVALID ((UIState)-1)
 enum UIState {
    MENU_INIT,               /**< Initial state */
    MENU_MAIN,               /**< Main menu */   

    MENU_MAIN_START_MOTOR,   /**< Motor Setup menu */ 
    MENU_MAIN_TIME_SELECT,   /**< Time selection submenu */
    MENU_MAIN_SPEED_CONTROL, /**< Speed control submenu */

    MENU_MAIN_REVIEW,        /**< Review submenu */
    MENU_REVIEW_SYSTEM,      /**< "System" option */
    MENU_REVIEW_SAVE_CONFIRM,/**< "Save data" option */
    MENU_REVIEW_SOFTWARE,    /**< "Software" option */

    MENU_POWER_OFF, /**< "Power Off" option */

    UI_STATE_COUNT           /**< Total number of states */
};

/**
 * @struct UIStateTable
 * @brief Associates a UI state with its event handler.
 */
struct UIStateTable {
    void (*onEnter)(void);
    void (*handleEvent)(EncoderEvent evt);
    void (*handleMovement)(int delta); /**< Batch-move handler; nullptr = fall back to N handleEvent calls */
    void (*onExit)(void);
};

void UI_setState(UIState newState);
void UI_processEvent(EncoderEvent evt);

/**
 * @brief Applies a net encoder delta to the current state in one render pass.
 *
 * Accumulates multiple ENC_LEFT / ENC_RIGHT ticks into a single logical move
 * and redraws only once. States that don't implement handleMovement fall back
 * to calling UI_processEvent for each individual step.
 *
 * @param delta Signed net tick count (positive = right / up, negative = left / down).
 */
void UI_processMovement(int delta);

static_assert(MENU_COUNT == 3, "MENU_COUNT must be 3 to match mainMenu arrays");

void UI_applySettings(const SettingsPayload& data);

#endif // UISTATE_H