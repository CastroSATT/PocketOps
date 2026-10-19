#include "hid/UsbHid.h"

#if ARDUINO_USB_MODE
#error TinyUSB required: set ARDUINO_USB_MODE=0 in platformio.ini
#endif

#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"

namespace {

USBHIDKeyboard g_keyboard;
USBHIDMouse g_mouse;

}  // namespace

namespace hid {

UsbHid& UsbHid::instance() {
  static UsbHid hid;
  return hid;
}

void UsbHid::registerDevices() {
  if (devices_) {
    return;
  }
  g_keyboard.begin();
  g_mouse.begin();
  devices_ = true;
}

void UsbHid::begin() {
  registerDevices();
  if (!bus_started_) {
    USB.begin();
    bus_started_ = true;
  }
  started_ = true;
}

bool UsbHid::ready() const { return started_; }

void UsbHid::typeText(const char* text) {
  if (!text) {
    return;
  }
  g_keyboard.print(text);
}

void UsbHid::keyDown(uint8_t keycode) { g_keyboard.press(keycode); }

void UsbHid::keyUp(uint8_t keycode) { g_keyboard.release(keycode); }

void UsbHid::keyTap(uint8_t keycode) {
  g_keyboard.press(keycode);
  delay(15);
  g_keyboard.release(keycode);
}

void UsbHid::writeChar(char c) { g_keyboard.write(c); }

void UsbHid::mouseMove(int8_t dx, int8_t dy, int8_t wheel) {
  g_mouse.move(dx, dy, wheel);
}

void UsbHid::mousePress(uint8_t button) { g_mouse.press(button); }

void UsbHid::mouseRelease(uint8_t button) { g_mouse.release(button); }

void UsbHid::mouseClick(uint8_t button) { g_mouse.click(button); }

}  // namespace hid
