#pragma once

#include "tools/Tool.h"
#include "tools/bt_serial_hid/BleSerialHidServer.h"

namespace tools {

class BtSerialHidTool : public Tool {
 public:
  const char* id() const override { return "bt_serial_hid"; }
  const char* name() const override { return "BT Serial HID"; }
  bool menuLedPulse(uint8_t& r, uint8_t& g, uint8_t& b) const override {
    r = 40;
    g = 220;
    b = 90;  // green
    return true;
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void draw() override;

 private:
  bt_serial_hid::BleSerialHidServer server_;
  uint32_t last_draw_ms_ = 0;
  uint8_t last_clients_ = 255;
};

}  // namespace tools
