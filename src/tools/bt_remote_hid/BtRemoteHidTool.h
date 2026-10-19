#pragma once

#include "tools/Tool.h"
#include "tools/bt_remote_hid/BleHidServer.h"

namespace tools {

class BtRemoteHidTool : public Tool {
 public:
  const char* id() const override { return "bt_remote_hid"; }
  const char* name() const override { return "BT Remote HID"; }
  bool menuLedPulse(uint8_t& r, uint8_t& g, uint8_t& b) const override {
    r = 200;
    g = 40;
    b = 255;  // violet
    return true;
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void draw() override;

 private:
  bt_remote_hid::BleHidServer server_;
  uint32_t last_draw_ms_ = 0;
  uint8_t last_clients_ = 255;
};

}  // namespace tools
