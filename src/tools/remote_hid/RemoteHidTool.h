#pragma once

#include "tools/Tool.h"
#include "tools/remote_hid/RemoteHidServer.h"

namespace tools {

class RemoteHidTool : public Tool {
 public:
  const char* id() const override { return "remote_hid"; }
  const char* name() const override { return "WiFi Remote HID"; }
  bool menuLedPulse(uint8_t& r, uint8_t& g, uint8_t& b) const override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void draw() override;

 private:
  remote_hid::RemoteHidServer server_;
  uint32_t last_draw_ms_ = 0;
  uint8_t last_clients_ = 255;
  char last_led_mode_[8] = {};
  uint8_t last_led_phase_ = 255;
};

}  // namespace tools
