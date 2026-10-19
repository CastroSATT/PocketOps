#include "tools/remote_hid/RemoteHidTool.h"

#include "WifiModeUi.h"
#include "display/Display.h"
#include "led/RgbLed.h"
#include "storage/ConfigStore.h"

#include <WiFi.h>

namespace {

bool softApUp() {
  const wifi_mode_t m = WiFi.getMode();
  return m == WIFI_AP || m == WIFI_AP_STA;
}

bool staUp() { return WiFi.status() == WL_CONNECTED; }

const char* liveLedMode() {
  return wifi_mode_ui::effectiveMode(storage::ConfigStore::instance().wifiMode(), softApUp(),
                                     staUp());
}

}  // namespace

namespace tools {

bool RemoteHidTool::menuLedPulse(uint8_t& r, uint8_t& g, uint8_t& b) const {
  const char* eff = liveLedMode();
  if (!eff[0]) {
    // Radio not up yet — fall back to configured mode colour.
    eff = storage::ConfigStore::instance().wifiMode();
  }
  const uint8_t phase =
      (eff && strcmp(eff, "apsta") == 0) ? static_cast<uint8_t>((millis() / 750) % 2) : 0;
  wifi_mode_ui::rgbLed(eff, phase, r, g, b);
  return true;
}

void RemoteHidTool::onEnter() {
  display::clear();
  display::statusBar("WiFi Remote HID");
  display::centeredText("Starting...", nullptr, TFT_YELLOW);

  server_.begin();
  last_clients_ = 255;
  last_draw_ms_ = 0;
  last_led_mode_[0] = '\0';
  last_led_phase_ = 255;
  draw();
}

void RemoteHidTool::onExit() { server_.end(); }

void RemoteHidTool::loop() {
  server_.loop();

  const char* eff = liveLedMode();
  if (!eff[0]) {
    eff = storage::ConfigStore::instance().wifiMode();
  }
  const uint8_t phase =
      (eff && strcmp(eff, "apsta") == 0) ? static_cast<uint8_t>((millis() / 750) % 2) : 0;
  if (strcmp(eff ? eff : "", last_led_mode_) != 0 || phase != last_led_phase_) {
    strncpy(last_led_mode_, eff ? eff : "", sizeof(last_led_mode_) - 1);
    last_led_mode_[sizeof(last_led_mode_) - 1] = '\0';
    last_led_phase_ = phase;
    uint8_t r = 0, g = 0, b = 0;
    wifi_mode_ui::rgbLed(eff, phase, r, g, b);
    led::RgbLed::instance().pulse(r, g, b);
  }

  const uint32_t now = millis();
  const uint8_t clients = server_.clientCount();
  if (clients != last_clients_ || (now - last_draw_ms_) > 2000) {
    last_clients_ = clients;
    last_draw_ms_ = now;
    draw();
  }
}

void RemoteHidTool::draw() {
  auto& tft = display::tft();
  display::clear();
  display::statusBar("WiFi Remote HID");

  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  char line[44];
  const IPAddress ap = server_.apIp();
  snprintf(line, sizeof(line), "AP %d.%d.%d.%d", ap[0], ap[1], ap[2], ap[3]);
  tft.drawString(line, 4, 18);

  const IPAddress sta = server_.staIp();
  if (sta[0] || sta[1] || sta[2] || sta[3]) {
    snprintf(line, sizeof(line), "LAN %d.%d.%d.%d", sta[0], sta[1], sta[2], sta[3]);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
  } else {
    snprintf(line, sizeof(line), "LAN -");
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  }
  tft.drawString(line, 4, 30);

  snprintf(line, sizeof(line), "Clients: %u", static_cast<unsigned>(server_.clientCount()));
  tft.setTextColor(server_.clientCount() ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(line, 4, 42);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("3x btn = back", tft.width() / 2, tft.height() - 6);
}

}  // namespace tools
