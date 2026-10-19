#include "tools/bt_serial_hid/BtSerialHidTool.h"

#include "config.h"
#include "display/Display.h"

namespace tools {

void BtSerialHidTool::onEnter() {
  display::clear();
  display::statusBar("BT Serial HID");
  display::centeredText("Starting NUS...", nullptr, TFT_YELLOW);

  server_.begin();
  last_clients_ = 255;
  last_draw_ms_ = 0;
  draw();
}

void BtSerialHidTool::onExit() { server_.end(); }

void BtSerialHidTool::loop() {
  server_.loop();

  const uint32_t now = millis();
  const uint8_t clients = server_.clientCount();
  if (clients != last_clients_ || (now - last_draw_ms_) > 2000) {
    last_clients_ = clients;
    last_draw_ms_ = now;
    draw();
  }
}

void BtSerialHidTool::draw() {
  auto& tft = display::tft();
  display::clear();
  display::statusBar("BT Serial HID");

  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  char line[40];
  snprintf(line, sizeof(line), "BLE: %s", config::BLE_SERIAL_DEVICE_NAME);
  tft.drawString(line, 4, 18);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("//key //mouse", 4, 30);
  tft.drawString("serial modes", 4, 42);

  const bool linked = server_.clientCount() > 0;
  tft.setTextColor(linked ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
  tft.drawString(linked ? "Status: linked" : "Status: advertising", 4, 54);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("3x btn = back", tft.width() / 2, tft.height() - 6);
}

}  // namespace tools
