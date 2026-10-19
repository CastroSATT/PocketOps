#pragma once

#include <Arduino.h>

namespace hid {

// Shared JSON command parser for all remote HID transports (Wi‑Fi, BLE, …).
// Payload shape matches the web UI: {t:'m'|'b'|'k'|'x', ...}
class HidProtocol {
 public:
  static void processMessage(const char* json);

 private:
  static int mapBrowserKey(int code);
  static uint8_t mapMouseButton(int index);
};

}  // namespace hid
