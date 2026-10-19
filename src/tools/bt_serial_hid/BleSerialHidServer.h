#pragma once

#include <Arduino.h>

namespace bt_serial_hid {

enum class SerialMode : uint8_t {
  Menu = 0,
  Key,
  Mouse,
};

// Nordic UART Service (NUS) transport for serial BLE apps (incl. iOS).
class BleSerialHidServer {
 public:
  void begin();
  void loop();
  void end();

  bool running() const { return running_; }
  uint8_t clientCount() const { return clients_; }
  void setClientCount(uint8_t n);
  void onNotifyEnabled();

  void feedBytes(const uint8_t* data, size_t len);
  void notifyLine(const char* line);

 private:
  bool tryMagic(const char* line);
  bool trySpecialKey(const char* cmd);
  void processMenuLine(const char* line);
  void processKeyLine(const char* line);
  bool handleMouseChar(char c);
  void flushLine();
  void sendHelp();
  void sendKeyCodes();
  void sendWelcome();
  void sendModeBanner();
  void setMode(SerialMode mode);

  bool running_ = false;
  uint8_t clients_ = 0;
  bool welcome_sent_ = false;
  uint32_t welcome_at_ms_ = 0;
  SerialMode mode_ = SerialMode::Menu;
  bool collecting_cmd_ = false;  // buffering a //… line in mouse mode
  char line_buf_[256] = {};
  size_t line_len_ = 0;
};

}  // namespace bt_serial_hid
