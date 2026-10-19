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
// Small TF/SD badge when USB expose is on (drawn by statusBar).
void drawStatusIcons();
// Call from App::loop — flashes SD badge during card R/W without full redraw.
void tickStatusIcons();
void centeredText(const char* line1, const char* line2 = nullptr,
                  uint16_t color = TFT_GREEN);

}  // namespace display
