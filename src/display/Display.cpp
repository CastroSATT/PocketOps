#include "display/Display.h"

#include <stdio.h>

#include "Branding.h"
#include "WifiModeUi.h"
#include "board_pins.h"
#include "storage/ConfigStore.h"
#include "storage/TfExpose.h"

#include <WiFi.h>

namespace {

TFT_eSPI g_tft;

bool g_icon_enabled = false;
bool g_icon_present = false;
bool g_icon_files_ui = false;
bool g_icon_flash_phase = false;
uint32_t g_sd_probe_ms = 0;
bool g_softap_up = false;
bool g_sta_up = false;
uint8_t g_wifi_led_phase = 255;
char g_wifi_mode_tag[4] = {};

constexpr int kBlChannel = 0;
constexpr int kBlFreqHz = 5000;
constexpr int kBlResolution = 8;
constexpr uint32_t kBlMaxDuty = (1u << kBlResolution) - 1u;

bool g_bl_pwm = false;
uint8_t g_brightness_pct = 80;
uint32_t g_sleep_sec = 0;
uint32_t g_last_activity_ms = 0;
bool g_asleep = false;

void writeBacklightDuty(uint8_t percent) {
  if (!g_bl_pwm) {
    return;
  }
  if (percent > 100) {
    percent = 100;
  }
  uint32_t duty = (static_cast<uint32_t>(percent) * kBlMaxDuty) / 100u;
  // Active-low backlight: high duty → pin high → lamp off.
  if (!pins::LCD_BL_ON_LEVEL) {
    duty = kBlMaxDuty - duty;
  }
  ledcWrite(kBlChannel, duty);
}

bool softApActive() {
  const wifi_mode_t m = WiFi.getMode();
  return m == WIFI_AP || m == WIFI_AP_STA;
}

bool staActive() { return WiFi.status() == WL_CONNECTED; }

void paintWifiModeBadge() {
  const char* mode = storage::ConfigStore::instance().wifiMode();
  const bool ap_up = softApActive();
  const bool sta_up = staActive();
  g_softap_up = ap_up;
  g_sta_up = sta_up;
  const char* eff = wifi_mode_ui::effectiveMode(mode, ap_up, sta_up);
  const char* tag = wifi_mode_ui::tag(eff);

  // Right cluster: [mode][gap][SD] — clear max badge slot first.
  const int sd_x = g_tft.width() - 22;
  const int max_w = 22;
  const int h = 10;
  const int slot_x = sd_x - 2 - max_w;
  const int y = 2;
  g_tft.fillRect(slot_x, y, max_w, h, TFT_NAVY);

  if (!tag[0]) {
    g_wifi_mode_tag[0] = '\0';
    g_wifi_led_phase = 255;
    return;
  }

  // Match APA102: AP sky / STA amber / both-live alternates (~750 ms).
  const uint8_t phase =
      (strcmp(eff, "apsta") == 0) ? static_cast<uint8_t>((millis() / 750) % 2) : 0;
  g_wifi_led_phase = phase;

  uint8_t r = 0, g = 0, b = 0;
  wifi_mode_ui::rgbLed(eff, phase, r, g, b);
  // Use TFT_eSPI helper so badge colours match LED/CSS under board RGB/BGR order.
  const uint16_t bg = g_tft.color565(r, g, b);
  const int w = (tag[2] != '\0') ? 22 : 18;
  const int x = sd_x - 2 - w;

  g_tft.fillRoundRect(x, y, w, h, 2, bg);
  g_tft.setTextDatum(MC_DATUM);
  g_tft.setTextFont(1);
  g_tft.setTextColor(TFT_BLACK, bg);
  g_tft.drawString(tag, x + w / 2, y + 5);
  strncpy(g_wifi_mode_tag, tag, sizeof(g_wifi_mode_tag) - 1);
  g_wifi_mode_tag[sizeof(g_wifi_mode_tag) - 1] = '\0';
}

void paintSdBadge(bool present, bool enabled, bool files_ui, bool flash_on) {
  const int x = g_tft.width() - 22;
  const int y = 2;
  const int w = 18;
  const int h = 10;

  if (!present && !enabled && !files_ui) {
    g_tft.fillRect(x, y, w, h, TFT_NAVY);
    return;
  }

  // Match web #sd-badge:
  // grey = card in / not exposed, orange = Files panel, yellow = Expose, red = activity flash.
  uint16_t bg;
  uint16_t fg = TFT_BLACK;
  if (files_ui) {
    if (flash_on) {
      bg = g_tft.color565(239, 68, 68);
      fg = TFT_WHITE;
    } else {
      bg = g_tft.color565(249, 115, 22);  // orange — Files open
    }
  } else if (enabled && flash_on) {
    bg = g_tft.color565(239, 68, 68);
    fg = TFT_WHITE;
  } else if (enabled) {
    bg = g_tft.color565(250, 204, 21);
  } else {
    bg = g_tft.color565(107, 114, 128);  // grey — inserted, Expose off
  }
  g_tft.fillRoundRect(x, y, w, h, 2, bg);
  g_tft.setTextDatum(MC_DATUM);
  g_tft.setTextFont(1);
  g_tft.setTextColor(fg, bg);
  g_tft.drawString("SD", x + 9, y + 5);
}

}  // namespace

namespace display {

void begin() {
  const int bl = pins::LCD_BL;
  ledcSetup(kBlChannel, kBlFreqHz, kBlResolution);
  ledcAttachPin(bl, kBlChannel);
  g_bl_pwm = true;
  writeBacklightDuty(0);  // off while init

  g_tft.init();
  g_tft.setRotation(static_cast<uint8_t>(pins::TFT_ROTATION));
  g_tft.fillScreen(TFT_BLACK);

  g_last_activity_ms = millis();
  g_asleep = false;
  writeBacklightDuty(g_brightness_pct);
}

void setBrightnessPercent(uint8_t percent) {
  if (percent < 5) {
    percent = 5;
  }
  if (percent > 100) {
    percent = 100;
  }
  g_brightness_pct = percent;
  if (!g_asleep) {
    writeBacklightDuty(g_brightness_pct);
  }
}

uint8_t brightnessPercent() { return g_brightness_pct; }

void setSleepTimeoutSec(uint32_t sec) {
  g_sleep_sec = sec;
  noteActivity();
}

uint32_t sleepTimeoutSec() { return g_sleep_sec; }

void noteActivity() {
  g_last_activity_ms = millis();
  if (g_asleep) {
    g_asleep = false;
    writeBacklightDuty(g_brightness_pct);
  }
}

void tickPower() {
  if (g_sleep_sec == 0 || g_asleep) {
    return;
  }
  const uint32_t idle_ms = millis() - g_last_activity_ms;
  if (idle_ms >= g_sleep_sec * 1000u) {
    g_asleep = true;
    writeBacklightDuty(0);
  }
}

bool asleep() { return g_asleep; }

void applyFromConfig() {
  auto& cfg = storage::ConfigStore::instance();
  setBrightnessPercent(cfg.displayBrightness());
  setSleepTimeoutSec(cfg.displaySleepSec());
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
  g_tft.drawString(storage::ConfigStore::instance().productName(), g_tft.width() / 2,
                   g_tft.height() / 2 - 8);

  char ver[24];
  snprintf(ver, sizeof(ver), "v%s", branding::kFirmwareVersion);
  g_tft.setTextFont(1);
  g_tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  g_tft.drawString(ver, g_tft.width() / 2, g_tft.height() / 2 + 12);
}

void drawStatusIcons() {
  auto& tf = storage::TfExpose::instance();
  if (!tf.enabled() && !tf.mounted() && millis() - g_sd_probe_ms > 4000u) {
    g_sd_probe_ms = millis();
    tf.refreshCardPresent();
  }
  const bool enabled = tf.enabled();
  const bool files_ui = tf.filesUiOpen() || tf.exclusiveHeld();
  const bool present =
      tf.cardPresent() || tf.mounted() || enabled || tf.exclusiveHeld() || files_ui;
  const bool busy =
      (files_ui && (tf.activityRecent() || tf.exclusiveHeld())) || (enabled && tf.activityRecent());
  const bool flash_on = busy && ((millis() / 90) & 1);
  g_icon_enabled = enabled;
  g_icon_present = present;
  g_icon_files_ui = files_ui;
  g_icon_flash_phase = flash_on;
  paintWifiModeBadge();
  paintSdBadge(present, enabled, files_ui, flash_on);
}

void tickStatusIcons() {
  auto& tf = storage::TfExpose::instance();
  if (!tf.enabled() && !tf.mounted() && millis() - g_sd_probe_ms > 4000u) {
    g_sd_probe_ms = millis();
    tf.refreshCardPresent();
  }
  const bool enabled = tf.enabled();
  const bool files_ui = tf.filesUiOpen() || tf.exclusiveHeld();
  const bool present =
      tf.cardPresent() || tf.mounted() || enabled || tf.exclusiveHeld() || files_ui;
  const bool busy =
      (files_ui && (tf.activityRecent() || tf.exclusiveHeld())) || (enabled && tf.activityRecent());
  const bool flash_on = busy && ((millis() / 90) & 1);
  const char* mode = storage::ConfigStore::instance().wifiMode();
  const bool ap_up = softApActive();
  const bool sta_up = staActive();
  const char* eff = wifi_mode_ui::effectiveMode(mode, ap_up, sta_up);
  const char* tag = wifi_mode_ui::tag(eff);
  const uint8_t led_phase =
      (strcmp(eff, "apsta") == 0) ? static_cast<uint8_t>((millis() / 750) % 2) : 0;
  const bool mode_changed = (strcmp(tag, g_wifi_mode_tag) != 0) || (ap_up != g_softap_up) ||
                            (sta_up != g_sta_up) || (led_phase != g_wifi_led_phase);

  if (enabled == g_icon_enabled && present == g_icon_present && files_ui == g_icon_files_ui &&
      flash_on == g_icon_flash_phase && !mode_changed) {
    return;
  }
  g_icon_enabled = enabled;
  g_icon_present = present;
  g_icon_files_ui = files_ui;
  g_icon_flash_phase = flash_on;
  if (mode_changed) {
    paintWifiModeBadge();
  }
  paintSdBadge(present, enabled, files_ui, flash_on);
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
