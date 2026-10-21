#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace hid {

// Shared command parser for remote HID transports (Wi‑Fi WS, BLE, …).
// JSON (legacy / BLE text): {t:'m'|'b'|'k'|'x', ...}
// Binary (Wi‑Fi WS hot path):
//   'M' dx dy wheel     — 4 bytes, mouse move
//   'B' btn state       — 3 bytes, btn 0=L 1=R 2=M, state 0/1
//   'K' keycode         — 2 bytes, browser key code
//   'X' + utf8…         — type text (rest of frame)
class HidProtocol {
 public:
  static void processMessage(const char* json);
  static void processBinary(const uint8_t* data, size_t len);

 private:
  static int mapBrowserKey(int code);
  static uint8_t mapMouseButton(int index);
};

}  // namespace hid
