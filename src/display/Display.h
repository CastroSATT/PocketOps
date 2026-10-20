#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

namespace display {

void begin();

TFT_eSPI& tft();

int width();
int height();

void clear(uint16_t color = TFT_BLACK);
// Boot splash: brand + firmware version (call after begin()).
void splash();
void statusBar(const char* title);
// Status-bar icons: Wi-Fi mode tag + TF/SD badge
// (grey=inserted, orange=Files, yellow=exposed).
void drawStatusIcons();
// Call from App::loop — flashes SD badge / refreshes mode tag without full redraw.
void tickStatusIcons();
void centeredText(const char* line1, const char* line2 = nullptr,
                  uint16_t color = TFT_GREEN);

// Backlight / sleep (pocketops_display.ini). Call applyFromConfig after loadDisplay().
void applyFromConfig();
void setBrightnessPercent(uint8_t percent);  // 5–100
uint8_t brightnessPercent();
void setSleepTimeoutSec(uint32_t sec);  // 0 = never
uint32_t sleepTimeoutSec();
void noteActivity();  // reset idle timer (button, etc.)
void tickPower();     // App::loop — enter/leave sleep
bool asleep();

}  // namespace display
