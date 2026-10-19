#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace remote_hid {

// SoftAP + HTTP page + WebSocket transport for remote HID.
class RemoteHidServer {
 public:
  void begin();
  void loop();
  void end();

  bool running() const { return running_; }
  uint8_t clientCount() const { return clients_; }
  void setClientCount(uint8_t n) { clients_ = n; }
  IPAddress apIp() const;

 private:
  bool running_ = false;
  uint8_t clients_ = 0;
};

}  // namespace remote_hid
