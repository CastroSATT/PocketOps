#include "storage/TfExpose.h"

#include <Preferences.h>
#include <SD_MMC.h>
#include <string.h>

#include "USBMSC.h"
#include "display/Display.h"
#include "hid/UsbHid.h"
#include "Branding.h"
#include "board_pins.h"
#include "storage/ConfigStore.h"
#include "storage/DeviceLog.h"
#include "storage/TfExposeInternal.h"
#include "storage/UsbConsole.h"
#include "tusb.h"


namespace storage {
using namespace tf_internal;


void TfExpose::markMscCooloff(uint32_t ms) {
  const uint32_t until = millis() + ms;
  if (until > msc_cooloff_until_ms_) {
    msc_cooloff_until_ms_ = until;
  }
}

void TfExpose::waitMscCooloff() {
  while (static_cast<int32_t>(msc_cooloff_until_ms_ - millis()) > 0) {
    delay(20);
    yield();
  }
}

void TfExpose::usbReenumerateHost() {
  // Forces macOS to drop a stale MSC volume after Expose off.
  if (!tud_connected()) {
    return;
  }
  PoSer().println("TF: USB re-enum (drop host volume)");
  tud_disconnect();
  delay(450);
  yield();
  tud_connect();
  delay(100);
  yield();
}
bool TfExpose::releaseMscAndUnmount(bool safe, FormatProgress progress) {
  auto tick = [&](int p, const char* log) {
    PoSer().println(log);
    if (progress) {
      progress(p, log);
    }
  };

  if (!enabled_ && !msc_started_) {
    if (mounted_) {
      unmountCard();
    }
    last_error_ = "";
    return true;
  }

  // Files safe-off: don't cut the bus while the Mac is mid-transfer.
  if (safe && activityRecent(500)) {
    last_error_ = "sd_busy";
    return false;
  }

  tick(5, "TF: expose off");
  enabled_ = false;
  g_msc.mediaPresent(false);
  delay(200);
  yield();
  g_msc.end();
  msc_started_ = false;

  // Same host settle loop format has always used (~3s).
  for (int i = 0; i < 6; ++i) {
    tick(6 + i, "TF: waiting host");
    delay(500);
    yield();
    if (safe && activityRecent(200)) {
      last_error_ = "sd_busy";
      unmountCard();
      markMscCooloff(2000);
      usbReenumerateHost();
      return false;
    }
  }

  tick(12, "TF: unmount");
  unmountCard();
  exclusive_ = false;
  exclusive_restore_expose_ = false;
  markMscCooloff(2000);
  usbReenumerateHost();
  last_error_ = "";
  return true;
}
bool TfExpose::startMsc() {
  if (!mounted_) {
    last_error_ = "no TF card";
    return false;
  }

  const uint32_t blocks = sectorCount();
  if (blocks == 0) {
    last_error_ = "bad geometry";
    return false;
  }

  auto& cfg = ConfigStore::instance();
  cfg.loadFormatFolders();
  // Vendor + revision are fixed; product follows the FAT drive name (volume label).
  g_msc.vendorID(branding::kName);
  g_msc.productID(cfg.volumeLabel());
  g_msc.productRevision(branding::kFirmwareVersion);
  g_msc.onRead(onRead);
  g_msc.onWrite(onWrite);
  g_msc.onStartStop(onStartStop);

  if (!g_msc.begin(blocks, 512)) {
    last_error_ = "msc fail";
    return false;
  }
  g_msc.mediaPresent(true);
  msc_started_ = true;

  hid::UsbHid::instance().begin();

  last_error_ = "";
  return true;
}

void TfExpose::stopMsc() {
  g_msc.mediaPresent(false);
  delay(100);   // let in-flight MSC read/write finish
  g_msc.end();  // clear callbacks + geometry
  msc_started_ = false;
}

bool TfExpose::enable() {
  if (enabled_) {
    return true;
  }
  // Browser exclusive mount must not leave MSC half-on.
  exclusive_ = false;
  exclusive_restore_expose_ = false;
  if (!mountCard()) {
    enabled_ = false;
    PO_LOGE("sd", "expose on failed: %s", last_error_ && last_error_[0] ? last_error_ : "mount");
    return false;
  }
  // Ensure /Logs (and configured folders) exist so the Mac sees a real layout.
  ConfigStore::instance().loadFormatFolders();
  ensureDirs();
  if (!startMsc()) {
    unmountCard();
    enabled_ = false;
    PO_LOGE("sd", "expose on failed: MSC");
    return false;
  }
  enabled_ = true;
  probe_ok_ = true;
  probed_ = true;
  host_eject_pending_ = false;

  // Soft USB re-plug so the host rediscovers the volume (same helper as off-path).
  usbReenumerateHost();
  PO_LOGI("sd", "expose on");
  return true;
}

void TfExpose::disable() {
  // Same teardown as Format’s “expose off” wait (not the short hard cut).
  // Does not touch boot_enabled_ / tf_boot.
  if (!releaseMscAndUnmount(false)) {
    // Force path should not fail; keep state consistent anyway.
    enabled_ = false;
    stopMsc();
    unmountCard();
    markMscCooloff(1500);
    usbReenumerateHost();
  }
  PO_LOGI("sd", "expose off");
}

bool TfExpose::disableSafe() {
  PoSer().println("TF: safe expose off");
  if (!releaseMscAndUnmount(true)) {
    PO_LOGW("sd", "safe-off refused/aborted: %s",
            last_error_ && last_error_[0] ? last_error_ : "busy");
    return false;
  }
  PO_LOGI("sd", "expose safe-off");
  return true;
}

bool TfExpose::toggle() {
  if (enabled_) {
    disable();
    return true;
  }
  return enable();
}


}  // namespace storage
