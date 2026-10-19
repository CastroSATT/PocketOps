#include "display/Display.h"

#include <stdio.h>

#include "board_pins.h"
#include "config.h"
#include "storage/TfExpose.h"

namespace {

TFT_eSPI g_tft;

bool g_icon_enabled = false;
bool g_icon_flash_phase = false;

void paintSdBadge(bool enabled, bool flash_on) {
  const int x = g_tft.width() - 22;
  const int y = 2;
  const int w = 18;
  const int h = 10;

  if (!enabled) {
    g_tft.fillRect(x, y, w, h, TFT_NAVY);
    return;
  }

  const uint16_t bg = flash_on ? TFT_YELLOW : TFT_CYAN;
  g_tft.fillRoundRect(x, y, w, h, 2, bg);
  g_tft.setTextDatum(MC_DATUM);
  g_tft.setTextFont(1);
  g_tft.setTextColor(TFT_BLACK, bg);
  g_tft.drawString("SD", x + 9, y + 5);
}

}  // namespace

namespace display {

void begin() {
  pinMode(pins::LCD_BL, OUTPUT);
  digitalWrite(pins::LCD_BL, HIGH);

  g_tft.init();
  g_tft.setRotation(1);
  g_tft.fillScreen(TFT_BLACK);

  digitalWrite(pins::LCD_BL, LOW);
}

TFT_eSPI& tft() { return g_tft; }

int width() { return g_tft.width(); }

int height() { return g_tft.height(); }

void clear(uint16_t color) { g_tft.fillScreen(color); }

void splash() {
  clear(TFT_BLACK);
  g_tft.setTextDatum(MC_DATUM);
  g_tft.setTextFont(2);
  g_tft.setTextColor(TFT_CYAN, TFT_BLACK);
  g_tft.drawString("PocketOps", g_tft.width() / 2, g_tft.height() / 2 - 8);

  char ver[24];
  snprintf(ver, sizeof(ver), "v%s", config::FIRMWARE_VERSION);
  g_tft.setTextFont(1);
  g_tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  g_tft.drawString(ver, g_tft.width() / 2, g_tft.height() / 2 + 12);
}

void drawStatusIcons() {
  auto& tf = storage::TfExpose::instance();
  const bool enabled = tf.enabled();
  const bool busy = enabled && tf.activityRecent();
  const bool flash_on = busy && ((millis() / 90) & 1);
  g_icon_enabled = enabled;
  g_icon_flash_phase = flash_on;
  paintSdBadge(enabled, flash_on);
}

void tickStatusIcons() {
  auto& tf = storage::TfExpose::instance();
  const bool enabled = tf.enabled();
  const bool busy = enabled && tf.activityRecent();
  const bool flash_on = busy && ((millis() / 90) & 1);

  if (enabled == g_icon_enabled && flash_on == g_icon_flash_phase) {
    return;
  }
  g_icon_enabled = enabled;
  g_icon_flash_phase = flash_on;
  paintSdBadge(enabled, flash_on);
}

void statusBar(const char* title) {
  g_tft.fillRect(0, 0, g_tft.width(), 14, TFT_NAVY);
  g_tft.setTextDatum(ML_DATUM);
  g_tft.setTextFont(1);
  g_tft.setTextColor(TFT_WHITE, TFT_NAVY);
  g_tft.drawString(title, 4, 7);
  drawStatusIcons();
}

void centeredText(const char* line1, const char* line2, uint16_t color) {
  g_tft.setTextDatum(MC_DATUM);
  g_tft.setTextFont(2);
  g_tft.setTextColor(color, TFT_BLACK);
  const int cy = line2 ? (g_tft.height() / 2 - 8) : (g_tft.height() / 2);
  g_tft.drawString(line1, g_tft.width() / 2, cy);
  if (line2) {
    g_tft.drawString(line2, g_tft.width() / 2, cy + 16);
  }
}

}  // namespace display
