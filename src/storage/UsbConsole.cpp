#include "storage/UsbConsole.h"

#include <Preferences.h>

#include "storage/ConfigStore.h"

namespace storage {
namespace {

class NullPrint : public Print {
 public:
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t* /*buf*/, size_t size) override { return size; }
};

NullPrint g_null;

}  // namespace

UsbConsole& UsbConsole::instance() {
  static UsbConsole s;
  return s;
}

void UsbConsole::load() {
  Preferences prefs;
  enabled_ = true;
  if (prefs.begin(ConfigStore::instance().nvsNamespace(), true)) {
    enabled_ = prefs.getBool("ser_on", true);
    prefs.end();
  }
  loaded_ = true;
}

void UsbConsole::persist() {
  Preferences prefs;
  if (!prefs.begin(ConfigStore::instance().nvsNamespace(), false)) {
    return;
  }
  prefs.putBool("ser_on", enabled_);
  prefs.end();
}

void UsbConsole::begin() {
  load();
  Serial.printf("USB console: %s\n", enabled_ ? "on" : "off");
}

void UsbConsole::setEnabled(bool on) {
  if (on == enabled_) {
    return;
  }
  enabled_ = on;
  persist();
  // Always print this one line so you can confirm the change over USB.
  Serial.printf("USB console: %s\n", enabled_ ? "on" : "off");
}

Print& UsbConsole::out() {
  if (!loaded_) {
    load();
  }
  return enabled_ ? static_cast<Print&>(Serial) : static_cast<Print&>(g_null);
}

}  // namespace storage
