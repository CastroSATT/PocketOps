#include "storage/FirmwareUpdate.h"

#include <Preferences.h>
#include <nvs_flash.h>

#if !ARDUINO_USB_MODE
#include "USB.h"
#endif

#include "storage/ConfigStore.h"
#include "storage/UsbConsole.h"

namespace storage {
namespace {

// Must match pocketops_folders.ini seed (ConfigStore may be empty in pre-arm).
constexpr const char* kNvsNs = "pocketops";

bool readDfuPrefEarly() {
  Preferences prefs;
  bool on = false;
  if (prefs.begin(kNvsNs, true)) {
    on = prefs.getBool("dfu_on", false);
    prefs.end();
  }
  return on;
}

const char* nvsNs() {
  const char* ns = ConfigStore::instance().nvsNamespace();
  return (ns && ns[0]) ? ns : kNvsNs;
}

}  // namespace

FirmwareUpdate& FirmwareUpdate::instance() {
  static FirmwareUpdate s;
  return s;
}

void FirmwareUpdate::noteArmed(bool armed) {
  instance().armed_ = armed;
}

void FirmwareUpdate::load() {
  Preferences prefs;
  dfu_on_ = false;
  if (prefs.begin(nvsNs(), true)) {
    dfu_on_ = prefs.getBool("dfu_on", false);
    prefs.end();
  }
  loaded_ = true;
}

void FirmwareUpdate::persist() {
  Preferences prefs;
  if (!prefs.begin(nvsNs(), false)) {
    return;
  }
  prefs.putBool("dfu_on", dfu_on_);
  prefs.end();
}

void FirmwareUpdate::begin() {
  load();
  PoSer().printf("Firmware DFU: pref=%s armed=%s\n", dfu_on_ ? "on" : "off",
                 armed_ ? "yes" : "no");
}

void FirmwareUpdate::setDfuEnabled(bool on) {
  if (!loaded_) {
    load();
  }
  if (on == dfu_on_) {
    return;
  }
  dfu_on_ = on;
  persist();
  PoSer().printf("Firmware DFU: pref=%s (reboot to apply USB arm)\n", dfu_on_ ? "on" : "off");
}

}  // namespace storage

#if !ARDUINO_USB_MODE
// Runs before app_main → USB.begin(). NVS must be up so we can honor default-off.
extern "C" void pocketops_dfu_prearm(void) __attribute__((constructor(101)));
void pocketops_dfu_prearm(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  bool armed = false;
  if (storage::readDfuPrefEarly()) {
    armed = USB.enableDFU();
  }
  storage::FirmwareUpdate::noteArmed(armed);
}
#endif
