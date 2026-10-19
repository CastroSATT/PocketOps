#pragma once

#include <Arduino.h>

namespace hid {

// Thin wrapper around TinyUSB keyboard + mouse
class UsbHid {
 public:
  static UsbHid& instance();

  // Register keyboard/mouse interfaces (no USB bus start yet).
  void registerDevices();
  // Register devices + start USB bus if needed.
  void begin();
  bool ready() const;
  bool busStarted() const { return bus_started_; }

  void typeText(const char* text);
  void keyDown(uint8_t keycode);
  void keyUp(uint8_t keycode);
  void keyTap(uint8_t keycode);
  void writeChar(char c);

  void mouseMove(int8_t dx, int8_t dy, int8_t wheel = 0);
  void mousePress(uint8_t button);
  void mouseRelease(uint8_t button);
  void mouseClick(uint8_t button);

 private:
  UsbHid() = default;
  bool devices_ = false;
  bool bus_started_ = false;
  bool started_ = false;
};

}  // namespace hid
