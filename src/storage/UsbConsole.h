#pragma once

#include <Arduino.h>
#include <Print.h>

namespace storage {

// USB CDC console (Serial). Can be muted from Config → Logs when deployed.
// Serial.begin() still runs — only writes are gated.
class UsbConsole {
 public:
  static UsbConsole& instance();

  void begin();  // load NVS after ConfigStore
  bool enabled() const { return enabled_; }
  void setEnabled(bool on);

  // Use for all console output: PoSer().printf(...);
  Print& out();

 private:
  UsbConsole() = default;
  void persist();
  void load();

  bool enabled_ = false;  // muted by default — enable in Config → Logs
  bool loaded_ = false;
};

}  // namespace storage

// Drop-in for Serial.print / printf / println (not Serial.begin).
inline Print& PoSer() { return storage::UsbConsole::instance().out(); }
