#include "tools/remote_hid/RemoteHidTool.h"

#include "config.h"
#include "display/Display.h"

namespace tools {

void RemoteHidTool::onEnter() {
  display::clear();
  display::statusBar("WiFi Remote HID");
  display::centeredText("Starting...", nullptr, TFT_YELLOW);

  server_.begin();
  last_clients_ = 255;
  last_draw_ms_ = 0;
  draw();
}

void RemoteHidTool::onExit() { server_.end(); }

void RemoteHidTool::loop() {
  server_.loop();

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

  char line[40];
  snprintf(line, sizeof(line), "WiFi: %s", config::WIFI_AP_SSID);
  tft.drawString(line, 4, 18);

  snprintf(line, sizeof(line), "Pass: %s", config::WIFI_AP_PASS);
  tft.drawString(line, 4, 30);

  const IPAddress ip = server_.apIp();
  snprintf(line, sizeof(line), "http://%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  tft.drawString(line, 4, 42);

  snprintf(line, sizeof(line), "Clients: %u", static_cast<unsigned>(server_.clientCount()));
  tft.setTextColor(server_.clientCount() ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(line, 4, 54);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("3x btn = back", tft.width() / 2, tft.height() - 6);
}

}  // namespace tools
