/**
 * @file UIState.cpp
 * @brief Implementation of the UI Finite State Machine.
 */
#include "UI/UIState.h"

// =====================
// MENU DEFINITIONS
// =====================
static const char* const mainMenuTitles[]    = {"INICIO", "SISTEMA", "APAGAR"};
static const uint16_t* const mainMenuIcons[] = { homeIcon, systemIcon, powerOffIcon };

static const char* const systemMenuTitles[]    = {"LIMPIEZA", "INFO", "GUARDAR"};
static const uint16_t* const systemMenuIcons[] = { homeIcon, aboutIcon, saveIcon };

static const char* const mainStartTitles[]    = {"TIMER", "VELOCIDAD"};
static const uint16_t* const mainStartIcons[] = { timerIcon, percentageIcon};

static const uint32_t timeOptions[] = {
    15 * 60 * 1000,
    30 * 60 * 1000,
    45 * 60 * 1000,
    60 * 60 * 1000,
    0
};

static const MotorCmdType cleanCmdMap[] =
{
    MOTOR_CMD_CLEAN_FAST,
    MOTOR_CMD_CLEAN_SLOW,
    MOTOR_CMD_CLEAN_PURGE,
    MOTOR_CMD_STOP
};

static constexpr uint32_t FAST_THRESHOLD_MS  = 80;
static constexpr uint32_t RESET_THRESHOLD_MS = 300;
static constexpr uint8_t  MAX_STEP           = 2;
static constexpr uint8_t  STEP_INCREMENT     = 1;

#define CLEAN_OPTION_COUNT (sizeof(cleanCmdMap)/sizeof(cleanCmdMap[0]))
static_assert(sizeof(timeOptions)/sizeof(timeOptions[0]) == TIME_OPTION_COUNT,
              "TIME_OPTION_COUNT in UIState.h must match timeOptions array size");

// =====================
// INTERNAL VARIABLES
// =====================
static int confirmIndex = 0;

static uint8_t timeIndex = TIME_DEFAULT_INDEX;
static uint8_t cleanModeIndex = 0;
static int motorSpeed = 0;

static UIState currentState = UI_STATE_INVALID;

static int currentMenu = 0;
static int lastMenu    = -1;

// =====================
// INTERNAL HELPERS
// =====================
static void UI_enterMenu(const char* const titles[], const uint16_t* const icons[], int items)
{
    currentMenu = 0;
    lastMenu    = 0;

    UI_drawMenu(titles, icons, items);
    UI_updateMenuSelection(titles, icons, lastMenu, currentMenu, items);
}

static void handleGenericMenu(EncoderEvent evt,
                              const char* const titles[],
                              const uint16_t* const icons[],
                              int optionCount,
                              const UIState* transitions)
{
    switch (evt) {
        case ENC_LEFT:
            currentMenu--;
            break;
        case ENC_RIGHT:
            currentMenu++;
            break;
        case BTN_SHORT:
            if (transitions && transitions[currentMenu] != UI_STATE_INVALID) {
                UI_setState(transitions[currentMenu]);
            }
            return;
        case BTN_LONG:
            UI_setState(MENU_MAIN);
            return;
        default:
            return;
    }

    if (currentMenu < 0) currentMenu = 0;
    if (currentMenu >= optionCount) currentMenu = optionCount - 1;

    if (currentMenu == lastMenu) return;
    UI_updateMenuSelection(titles, icons, lastMenu, currentMenu, optionCount);
    lastMenu = currentMenu;
}

static void handleConfirmDialog(EncoderEvent evt, void (*onAccept)(void), UIState acceptState, UIState cancelState)
{
    switch (evt)
    {
        case ENC_LEFT:
            if (confirmIndex == 0) break;
            confirmIndex = 0;
            UI_drawConfirmButtons(confirmIndex);
            break;
        case ENC_RIGHT:
            if (confirmIndex == 1) break;
            confirmIndex = 1;
            UI_drawConfirmButtons(confirmIndex);
            break;
        case BTN_SHORT:
            if (confirmIndex == 0)
            {
                onAccept();
                sendBuzzerCommand(BUZZER_CMD_CONFIRM);
                UI_setState(acceptState);
                return;
            }
            UI_setState(cancelState);
            break;
        case BTN_LONG:
            UI_setState(cancelState);
            break;
        default:
            break;
    }
}

static inline uint8_t dinamicEncoder(uint32_t deltaTime, uint8_t& currentStep)
{
    
    if (deltaTime < FAST_THRESHOLD_MS) {
        uint8_t newStep = currentStep + STEP_INCREMENT;
        currentStep = (newStep > MAX_STEP) ? MAX_STEP : newStep;
    } 
    else if (deltaTime > RESET_THRESHOLD_MS) {
        currentStep = 1;
    }
    
    return currentStep;
}

static inline uint8_t clampIndex(int v, uint8_t max)
{
    if(v < 0) return 0;
    if(v >= max) return max - 1;
    return (uint8_t)v;
}

/**
 * @brief Restores motor speed and time index from persisted NVS settings.
 */
void UI_applySettings(const SettingsPayload& data)
{
    motorSpeed = data.motorSpeed;
    timeIndex  = data.timeIndex;
}

static void OnsendSettingsSave()
{
    sendSettingsSave(SETTINGS_CMD_SAVE, motorSpeed, timeIndex);
}

static void OnsendPowerRequest()
{
    sendPowerRequest(POWER_CMD_SHUTDOWN);
}

// =====================
// ENTRY HOOKS
// =====================
static void enterInit()
{
    UI_drawBootLogo();
    sendBuzzerCommand(BUZZER_CMD_INIT);
}

static void enterMainMenu()
{
    UI_enterMenu(mainMenuTitles, mainMenuIcons, MENU_COUNT);
}

static void enterStartMenu()
{
    UI_enterMenu(mainStartTitles, mainStartIcons, MENU_COUNT-1);
}

static void enterSystemMenu()
{
    UI_enterMenu(systemMenuTitles, systemMenuIcons, MENU_COUNT);
}

static void enterTimeSelect()
{
    UI_drawTimeSelectStatic();
    UI_updateTimeSelect(timeIndex);
}

static void enterSpeedControl()
{
    UI_drawSpeedStatic();
    UI_updateSpeed(motorSpeed);
}

static void enterSystem()
{
    UI_drawReviewSystem();
    UI_updateSystemSelect(cleanModeIndex);
}

static void enterSaveConfirm()
{
    confirmIndex = 0;
    UI_drawConfirmStatic("GUARDAR?", saveIcon);
}

static void enterPowerOff()
{
    confirmIndex = 0;
    UI_drawConfirmStatic("APAGAR?", powerOffIcon);
}

// =====================
// EVENT HANDLERS
// =====================
static void handleInit(EncoderEvent evt)
{
    if(evt == ENC_LEFT || evt == ENC_RIGHT)
    {
        UI_setState(MENU_MAIN);
    }
}

static void handleMainMenu(EncoderEvent evt)
{
    static const UIState transitions[] = {
        MENU_MAIN_START_MOTOR,
        MENU_MAIN_REVIEW,
        MENU_POWER_OFF
    };

    handleGenericMenu(evt,
                      mainMenuTitles,
                      mainMenuIcons,
                      MENU_COUNT,
                      transitions);
}

static void handleMainStart(EncoderEvent evt)
{
    static const UIState transitions[] = {
        MENU_MAIN_TIME_SELECT,
        MENU_MAIN_SPEED_CONTROL
    };

    handleGenericMenu(evt,
                      mainStartTitles,
                      mainStartIcons,
                      MENU_COUNT-1,
                      transitions);
}

static void handleTimeSelect(EncoderEvent evt)
{
    uint8_t prev = timeIndex;
    switch(evt)
    {
        case ENC_LEFT:
            timeIndex = clampIndex(timeIndex - 1, TIME_OPTION_COUNT);
            break;
        case ENC_RIGHT:
            timeIndex = clampIndex(timeIndex + 1, TIME_OPTION_COUNT);
            break;
        case BTN_SHORT:
            if (timeOptions[timeIndex] == 0) {
                /* PARAR option: stop all motor operations immediately. */
                sendMotorRequest(MOTOR_CMD_STOP, 0, 0);
            }
            sendBuzzerCommand(BUZZER_CMD_CONFIRM);
            return;
        case BTN_LONG:
            UI_setState(MENU_MAIN_START_MOTOR);
            return;
        default: return;
    }
    if (timeIndex != prev)
        UI_updateTimeSelect(timeIndex);
}

static void handleSpeedControl(EncoderEvent evt)
{
    static uint32_t lastChangeTime = 0;
    static uint8_t speedStep = 1;
    static bool wasAtLimit = false;
    
    switch(evt)
    {
        case ENC_LEFT:
        case ENC_RIGHT:
        {
            uint32_t now = millis();
            uint8_t step = dinamicEncoder(now - lastChangeTime, speedStep);
            lastChangeTime = now;

            int newSpeed = (evt == ENC_LEFT) ? motorSpeed - step : motorSpeed + step;
            bool hitLimit = (newSpeed < 0) || (newSpeed > 100);

            motorSpeed = constrain(newSpeed, 0, 100);

            if (hitLimit && !wasAtLimit) {
                speedStep = 1;
                sendBuzzerCommand(BUZZER_CMD_ERROR);
                wasAtLimit = true;
            } else if (!hitLimit) {
                wasAtLimit = false;
            }
            break;
        }
        case BTN_SHORT:
            if (timeOptions[timeIndex] == 0) {
                /* PARAR selected — run continuous (no timeout). */
                sendMotorRequest(MOTOR_CMD_SET_SPEED, motorSpeed, 0);
            } else {
                sendMotorRequest(MOTOR_CMD_START_TIMED, motorSpeed, timeOptions[timeIndex]);
            }
            sendBuzzerCommand(BUZZER_CMD_CONFIRM);
            speedStep = 1;
            wasAtLimit = false;
            return;
        case BTN_LONG:
            speedStep = 1;
            wasAtLimit = false;
            UI_setState(MENU_MAIN_START_MOTOR);
            return;
        default: return;
    }
    
    UI_updateSpeed(motorSpeed);
}

static void handleSystemMenu(EncoderEvent evt)
{
    static const UIState transitions[] = {
        MENU_REVIEW_SYSTEM,
        MENU_REVIEW_SOFTWARE,
        MENU_REVIEW_SAVE_CONFIRM
    };

    handleGenericMenu(evt,
                      systemMenuTitles,
                      systemMenuIcons,
                      MENU_COUNT,
                      transitions);
}

static void handleSystem(EncoderEvent evt)
{
    uint8_t prev = cleanModeIndex;
    switch(evt)
    {
        case ENC_LEFT:
            cleanModeIndex = clampIndex(cleanModeIndex - 1, CLEAN_OPTION_COUNT);
            break;
        case ENC_RIGHT:
            cleanModeIndex = clampIndex(cleanModeIndex + 1, CLEAN_OPTION_COUNT);
            break;
        case BTN_SHORT:
            if (sendMotorRequest(cleanCmdMap[cleanModeIndex], 0, 0))
                sendBuzzerCommand(BUZZER_CMD_CONFIRM);
            else
                sendBuzzerCommand(BUZZER_CMD_ERROR);
            return;
        case BTN_LONG:
            UI_setState(MENU_MAIN_REVIEW);
            return;
        default: return;
    }
    if (cleanModeIndex != prev)
        UI_updateSystemSelect(cleanModeIndex);
}

static void handleSaveConfirm(EncoderEvent evt)
{
    handleConfirmDialog(evt, OnsendSettingsSave, MENU_MAIN, MENU_MAIN_REVIEW);
}

static void handleSoftInfo(EncoderEvent evt)
{
    if (evt == BTN_LONG)
        UI_setState(MENU_MAIN_REVIEW);
}

static void handleSettingsPowerOff(EncoderEvent evt)
{
    handleConfirmDialog(evt, OnsendPowerRequest, MENU_INIT, MENU_MAIN);
}

// =====================
// MOVEMENT HANDLERS
// =====================
static inline int clampDelta(int index, int delta, int count)
{
    int result = index + delta;
    if (result < 0)      return 0;
    if (result >= count) return count - 1;
    return result;
}

static void handleMainMenuMovement(int delta)
{
    int next = clampDelta(currentMenu, delta, MENU_COUNT);
    if (next == currentMenu) return;
    currentMenu = next;
    UI_updateMenuSelection(mainMenuTitles, mainMenuIcons, lastMenu, currentMenu, MENU_COUNT);
    lastMenu = currentMenu;
}

static void handleStartMenuMovement(int delta)
{
    int next = clampDelta(currentMenu, delta, MENU_COUNT - 1);
    if (next == currentMenu) return;
    currentMenu = next;
    UI_updateMenuSelection(mainStartTitles, mainStartIcons, lastMenu, currentMenu, MENU_COUNT - 1);
    lastMenu = currentMenu;
}

static void handleSystemMenuMovement(int delta)
{
    int next = clampDelta(currentMenu, delta, MENU_COUNT);
    if (next == currentMenu) return;
    currentMenu = next;
    UI_updateMenuSelection(systemMenuTitles, systemMenuIcons, lastMenu, currentMenu, MENU_COUNT);
    lastMenu = currentMenu;
}

static void handleTimeSelectMovement(int delta)
{
    uint8_t next = (uint8_t)clampDelta(timeIndex, delta, TIME_OPTION_COUNT);
    if (next == timeIndex) return;
    timeIndex = next;
    UI_updateTimeSelect(timeIndex);
}

static void handleSystemSelectMovement(int delta)
{
    uint8_t next = (uint8_t)clampDelta(cleanModeIndex, delta, CLEAN_OPTION_COUNT);
    if (next == cleanModeIndex) return;
    cleanModeIndex = next;
    UI_updateSystemSelect(cleanModeIndex);
}

static void handleConfirmMovement(int delta)
{
    int next = clampDelta(confirmIndex, delta, 2);
    if (next == confirmIndex) return;
    confirmIndex = next;
    UI_drawConfirmButtons(confirmIndex);
}

// =====================
// STATE TABLE
// =====================
static const UIStateTable stateTable[] = {

    // MENU_INIT
    { enterInit,         handleInit,             nullptr,                    nullptr },

    // MENU_MAIN
    { enterMainMenu,     handleMainMenu,          handleMainMenuMovement,     nullptr },

    // MENU_MAIN_START_MOTOR
    { enterStartMenu,    handleMainStart,         handleStartMenuMovement,    nullptr },

    // MENU_MAIN_TIME_SELECT
    { enterTimeSelect,   handleTimeSelect,        handleTimeSelectMovement,   nullptr },

    // MENU_MAIN_SPEED_CONTROL — dynamic encoder step logic; falls back to per-event calls
    { enterSpeedControl, handleSpeedControl,      nullptr,                    nullptr },

    // MENU_MAIN_REVIEW
    { enterSystemMenu,   handleSystemMenu,        handleSystemMenuMovement,   nullptr },

    // MENU_REVIEW_SYSTEM
    { enterSystem,       handleSystem,            handleSystemSelectMovement, nullptr },

    // MENU_REVIEW_SAVE_CONFIRM
    { enterSaveConfirm,  handleSaveConfirm,       handleConfirmMovement,      nullptr },

    // MENU_REVIEW_SOFTWARE
    { UI_drawReviewSoft, handleSoftInfo,          nullptr,                    nullptr },

    // MENU_POWER_OFF
    { enterPowerOff,     handleSettingsPowerOff,  handleConfirmMovement,      nullptr },
};

// =====================
// FSM API
// =====================
/**
 * @brief Transitions to a new UI state, invoking onExit/onEnter callbacks.
 *
 * @param newState Target state; no-op if equal to the current state.
 */
void UI_setState(UIState newState)
{
    if (newState >= UI_STATE_COUNT) return;
    if (newState == currentState)   return;

    if (currentState != UI_STATE_INVALID)
    {
        if (stateTable[currentState].onExit)
            stateTable[currentState].onExit();
    }

    currentState = newState;

    if (stateTable[currentState].onEnter)
        stateTable[currentState].onEnter();
}

/**
 * @brief Dispatches an encoder event to the current state's handler.
 *
 * @param evt Encoder event to process.
 */
void UI_processEvent(EncoderEvent evt)
{
    if (stateTable[currentState].handleEvent)
        stateTable[currentState].handleEvent(evt);
}

void UI_processMovement(int delta)
{
    if (delta == 0 || currentState >= UI_STATE_COUNT) return;

    if (stateTable[currentState].handleMovement)
    {
        stateTable[currentState].handleMovement(delta);
    }
    else
    {
        /* Fall back for states with complex per-tick logic (e.g. speed control). */
        EncoderEvent dir = (delta > 0) ? ENC_RIGHT : ENC_LEFT;
        int steps = abs(delta);
        for (int i = 0; i < steps; i++)
            UI_processEvent(dir);
    }
}

static_assert(UI_STATE_COUNT == (sizeof(stateTable) / sizeof(stateTable[0])));