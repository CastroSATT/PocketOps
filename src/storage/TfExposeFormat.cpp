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


bool TfExpose::formatPocketLayout(TfCardInfo& out, FormatProgress progress) {
  out = {};
  // No Serial.flush here — with MSC active, CDC flush can wedge the USB stack.
  auto tick = [&](int p, const char* log) {
    PoSer().println(log);
    if (progress) {
      progress(p, log);
    }
  };

  // Same host release as Files safe-off / Expose disable (shared path).
  if (!releaseMscAndUnmount(false, progress)) {
    return false;
  }
  delay(100);

  // Always raw-format: works for exFAT / corrupt FAT (no VFS mount required).
  tick(15, "TF: wipe→FAT32");
  if (!rawFormatFat32(progress)) {
    last_error_ = "format fail";
    probe_ok_ = false;
    probed_ = true;
    return false;
  }

  tick(80, "TF: mount");
  if (!tryBegin(true, 400, false) && !tryBegin(false, 400, false) &&
      !tryBegin(false, SDMMC_FREQ_DEFAULT, false)) {
    last_error_ = "remount fail";
    probe_ok_ = false;
    probed_ = true;
    return false;
  }
  mounted_ = true;

  tick(88, "TF: folders");
  ConfigStore::instance().loadFormatFolders();
  if (!ensureDirs()) {
    last_error_ = "mkdir fail";
    fillInfo(out);
    unmountCard();
    return false;
  }
  fillInfo(out);

  // Re-stamp label after mkdir (safe, no dir shifting) so Mac definitely sees it.
  tick(95, "TF: label");
  SD_MMC.end();
  mounted_ = false;
  if (!rawApplyVolumeLabel()) {
    last_error_ = "label fail";
    probe_ok_ = false;
    probed_ = true;
    return false;
  }

  probe_ok_ = true;
  probed_ = true;
  last_error_ = "";
  tick(100, "TF: format OK");
  PoSer().printf("TF: format OK size=%uMB label=%s\n", out.size_mb, configuredVolumeLabel());
  PO_LOGI("sd", "format OK %uMB label=%s", static_cast<unsigned>(out.size_mb),
          configuredVolumeLabel());
  return true;
}


}  // namespace storage
