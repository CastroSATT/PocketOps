#pragma once

#include <Arduino.h>

namespace bt_remote_hid {

// BLE GATT transport for remote HID (desktop Web Bluetooth).
class BleHidServer {
 public:
  void begin();
  void loop();
  void end();

  bool running() const { return running_; }
  uint8_t clientCount() const { return clients_; }
  void setClientCount(uint8_t n) { clients_ = n; }

 private:
  bool running_ = false;
  uint8_t clients_ = 0;
};

}  // namespace bt_remote_hid
