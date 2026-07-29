/**
 * @file UI.cpp
 * @brief TFT display rendering implementation.
 */
#include "UI/UI.h"
#include "Config/pins.h"
#include "images/icons.h"
#include <TFT_eSPI.h>

#define VERSION_STRING "v2.4.0"

static constexpr int SCREEN_WIDTH  = 160;
static constexpr int SCREEN_HEIGHT = 128;

static constexpr int ICON_X      = 4;
static constexpr int TEXT_X      = 44;
static constexpr int TEXT_HEIGHT = 16;

static constexpr int BTN_Y = 80;
static constexpr int BTN_W = 60;
static constexpr int BTN_H = 40;

static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite iconSprite = TFT_eSprite(&tft);
static TFT_eSprite bigIconSprite = TFT_eSprite(&tft);
static TFT_eSprite logoIconSprite = TFT_eSprite(&tft);

/**
 * @brief Initializes TFT display, sprite buffers, and backlight GPIO.
 *
 * The backlight is active-HIGH (HIGH = on). It is enabled here only after the
 * TFT driver and sprites are set up and the screen has been cleared to black,
 * so no driver-startup noise is shown.
 */
void UI_init()
{
    tft.init();
    tft.setSwapBytes(true);
    tft.setRotation(3);
    tft.setTextSize(2);
    tft.fillScreen(TFT_BLACK);

    iconSprite.setSwapBytes(true);
    iconSprite.createSprite(ICON_WIDTH, ICON_HEIGHT);

    bigIconSprite.setSwapBytes(true);
    bigIconSprite.createSprite(ICON_WIDTH*2, ICON_HEIGHT*2);

    logoIconSprite.setSwapBytes(true);
    logoIconSprite.createSprite(ICON_WIDTH*4, ICON_HEIGHT*4);

    pinMode(PIN_TFT_LED, OUTPUT);
    digitalWrite(PIN_TFT_LED, HIGH);
}

void UI_drawIcon(int16_t x, int16_t y, const uint16_t* icon)
{
    iconSprite.fillSprite(TFT_BLACK);
    iconSprite.pushImage(0, 0, ICON_WIDTH, ICON_HEIGHT, icon);
    iconSprite.pushSprite(x, y, TFT_BLACK);
}

static void UI_drawMenuItem(const char* title, const uint16_t* icon, int y, int sectionHeight, bool selected)
{
    int iconY = y + (sectionHeight - ICON_HEIGHT) / 2;
    int textY = y + (sectionHeight - TEXT_HEIGHT) / 2;

    uint16_t bg = selected ? TFT_WHITE : TFT_BLACK;
    uint16_t fg = selected ? TFT_BLACK : TFT_DARKGREY;

    tft.fillRect(0, y, SCREEN_WIDTH, sectionHeight + 2, bg);

    if(icon)
        UI_drawIcon(ICON_X, iconY, icon);
    tft.setTextColor(fg, bg);
    tft.setCursor(TEXT_X, textY);
    tft.print(title);
}

void UI_drawMenu(const char* const titles[], const uint16_t* const icons[], int items)
{
    const int sectionHeight = SCREEN_HEIGHT / items;

    for(int i = 0; i < items; i++)
        UI_drawMenuItem(titles[i], icons[i], i * sectionHeight, sectionHeight, false);
}

void UI_updateMenuSelection(const char* const* titles, const uint16_t* const* icons, int last, int current, int items)
{
    const int sectionHeight = SCREEN_HEIGHT / items;

    if(last >= 0)
        UI_drawMenuItem(titles[last], icons[last], last * sectionHeight, sectionHeight, false);

    if(current >= 0)
        UI_drawMenuItem(titles[current], icons[current], current * sectionHeight, sectionHeight, true);
}

static void UI_drawHeader(const char* title, const uint16_t* icon)
{
    const int sectionHeight = SCREEN_HEIGHT / MENU_COUNT;
    tft.fillScreen(TFT_BLACK);
    UI_drawMenuItem(title, icon, 0, sectionHeight, true);
}

void UI_drawConfirmStatic(const char* title, const uint16_t* icon)
{
    UI_drawHeader(title, icon);
    UI_drawConfirmButtons(0);
}

void UI_drawConfirmButtons(int selected)
{
    const int center = SCREEN_WIDTH / 2;

    int yesX = center - BTN_W - 10;
    int noX  = center + 10;

    tft.fillRect(yesX, BTN_Y, BTN_W, BTN_H, selected == 0 ? TFT_WHITE : TFT_BLACK);
    tft.drawRect(yesX, BTN_Y, BTN_W, BTN_H, TFT_WHITE);

    tft.fillRect(noX, BTN_Y, BTN_W, BTN_H, selected == 1 ? TFT_WHITE : TFT_BLACK);
    tft.drawRect(noX, BTN_Y, BTN_W, BTN_H, TFT_WHITE);

    tft.setTextDatum(MC_DATUM);

    tft.setTextColor(selected == 0 ? TFT_BLACK : TFT_WHITE);
    tft.drawString("SI", yesX + BTN_W / 2, BTN_Y + BTN_H / 2);

    tft.setTextColor(selected == 1 ? TFT_BLACK : TFT_WHITE);
    tft.drawString("NO", noX + BTN_W / 2, BTN_Y + BTN_H / 2);
}

void UI_drawBootLogo()
{
    tft.fillScreen(TFT_WHITE);

    const int iconSize = ICON_WIDTH * 4;
    logoIconSprite.fillSprite(TFT_BLACK);
    logoIconSprite.pushImage(0, 0, iconSize, iconSize, logoIcon);
    logoIconSprite.pushSprite(SCREEN_WIDTH/2 - iconSize/2, SCREEN_HEIGHT/2 - iconSize/2, TFT_BLACK);

    tft.setFreeFont(&FreeSerifBoldItalic9pt7b);

    const char* text1 = "Bio";
    const char* text2 = "Gelato";

    int w1 = tft.textWidth(text1);
    int w2 = tft.textWidth(text2);
    int totalWidth = w1 + w2;

    int x = (SCREEN_WIDTH - totalWidth) / 2;
    int y = SCREEN_HEIGHT / 2;

    tft.setTextColor(TFT_BLUE);
    tft.drawString(text1, x, y);

    tft.setTextColor(TFT_DARKGREEN);
    tft.drawString(text2, x + w1, y);

    tft.setFreeFont(nullptr);
}

void UI_drawSpeedStatic()
{
    UI_drawHeader("VELOCIDAD", percentageIcon);
}

void UI_updateSpeed(int speed)
{
    static int prevSpeed = -1;
    char buf[8];

    int centerX = SCREEN_WIDTH  / 2 + 70;
    int centerY = SCREEN_HEIGHT / 2 + 20;

    tft.setTextDatum(MR_DATUM);
    tft.setFreeFont(&FreeSans24pt7b);

    /* Erase only the previous number's pixels instead of blanking the whole area. */
    if (prevSpeed >= 0 && prevSpeed != speed) {
        snprintf(buf, sizeof(buf), "%d", prevSpeed);
        tft.setTextColor(TFT_BLACK, TFT_BLACK);
        tft.drawString(buf, centerX, centerY);
    }

    snprintf(buf, sizeof(buf), "%d", speed);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(buf, centerX, centerY);

    prevSpeed = speed;
    tft.setFreeFont(nullptr);
}

static const char* const timeLabels[] = {
    "15 MIN", "30 MIN", "45 MIN", "60 MIN", "PARAR"
};

void UI_drawTimeSelectStatic()
{
    UI_drawHeader("TIMER", timerIcon);
}

void UI_updateTimeSelect(int index)
{
    tft.fillRect(0, SCREEN_HEIGHT / MENU_COUNT, SCREEN_WIDTH, SCREEN_HEIGHT, TFT_BLACK);

    tft.setTextDatum(MR_DATUM);
    tft.setFreeFont(&FreeSans12pt7b);
    tft.setTextColor(TFT_BLUE, TFT_BLACK);

    int centerX = SCREEN_WIDTH  / 2 + 70;
    int centerY = SCREEN_HEIGHT / 2 + 20;
    tft.drawString(timeLabels[index], centerX, centerY);

    tft.setFreeFont(nullptr);
}

static const char* const cleanLabels[] = {
    "RAPIDO", "LENTO", "PURGA", "PARAR"
};

void UI_drawReviewSystem()
{
    UI_drawHeader("LIMPIEZA", nullptr);
}

void UI_updateSystemSelect(int index)
{
    tft.fillRect(0, SCREEN_HEIGHT / MENU_COUNT, SCREEN_WIDTH, SCREEN_HEIGHT, TFT_BLACK);

    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(TFT_BLUE, TFT_BLACK);

    int centerX = SCREEN_WIDTH  / 2;
    int centerY = SCREEN_HEIGHT / 2 + 20;
    tft.drawString(cleanLabels[index], centerX, centerY);

    tft.setFreeFont(nullptr);
}

void UI_drawReviewSoft()
{
    tft.fillScreen(TFT_BLACK);
    UI_drawHeader(VERSION_STRING, nullptr);

    const int iconSize = ICON_WIDTH * 2;

    bigIconSprite.fillSprite(TFT_BLACK);
    bigIconSprite.pushImage(0, 0, iconSize, iconSize, QRIcon);
    bigIconSprite.pushSprite(SCREEN_WIDTH/2 -35, SCREEN_HEIGHT/2 - 10, TFT_BLACK);
}