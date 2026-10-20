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



TfExpose& TfExpose::instance() {
  static TfExpose s;
  return s;
}

void TfExpose::noteActivity() { activity_ms_ = millis(); }

bool TfExpose::activityRecent(uint32_t window_ms) const {
  if (activity_ms_ == 0) {
    return false;
  }
  return (millis() - activity_ms_) < window_ms;
}

void TfExpose::noteFilesUi(bool open) {
  if (open) {
    files_ui_ms_ = millis();
    if (files_ui_ms_ == 0) {
      files_ui_ms_ = 1;
    }
  } else {
    files_ui_ms_ = 0;
  }
}

bool TfExpose::filesUiOpen(uint32_t window_ms) const {
  if (files_ui_ms_ == 0) {
    return false;
  }
  return (millis() - files_ui_ms_) < window_ms;
}

void TfExpose::persistBoot() {
  Preferences prefs;
  ConfigStore::instance().loadFormatFolders();
  if (!prefs.begin(ConfigStore::instance().nvsNamespace(), false)) {
    return;
  }
  prefs.putBool("tf_boot", boot_enabled_);
  prefs.end();
}

void TfExpose::setBootEnabled(bool on) {
  boot_enabled_ = on;
  persistBoot();
  PO_LOGI("sd", "expose on boot %s", boot_enabled_ ? "on" : "off");
}

void TfExpose::noteHostEject() {
  enabled_ = false;  // onRead/onWrite refuse immediately
  host_eject_pending_ = true;
}

void TfExpose::tick() {
  if (!host_eject_pending_) {
    return;
  }
  host_eject_pending_ = false;
  PoSer().println("TF: host ejected — Expose off (boot setting unchanged)");
  PO_LOGI("sd", "host eject → expose off");
  disable();
}

void TfExpose::loadSaved() {
  delay(300);
  refreshCardPresent();

  Preferences prefs;
  boot_enabled_ = false;
  ConfigStore::instance().loadFormatFolders();
  if (prefs.begin(ConfigStore::instance().nvsNamespace(), true)) {
    // New key tf_boot; default from legacy tf_on so existing devices keep behavior.
    const bool legacy = prefs.getBool("tf_on", false);
    boot_enabled_ = prefs.getBool("tf_boot", legacy);
    prefs.end();
  }
  // Migrate legacy into tf_boot once.
  persistBoot();
  if (boot_enabled_) {
    enable();
  }
}
bool TfExpose::refreshCardPresent() {
  if (mounted_ || exclusive_) {
    probe_ok_ = true;
    probed_ = true;
    return true;
  }
  // Do not thrash SD_MMC during MSC cool-off (safe-off → Files).
  if (static_cast<int32_t>(msc_cooloff_until_ms_ - millis()) > 0) {
    return probe_ok_;
  }

  if (!mountCard()) {
    probe_ok_ = false;
    probed_ = true;
    return false;
  }
  if (!enabled_) {
    unmountCard();
  }
  probe_ok_ = true;
  probed_ = true;
  return true;
}

void TfExpose::fillInfo(TfCardInfo& out) {
  out = {};
  out.present = mounted_ && SD_MMC.cardType() != CARD_NONE;
  if (!out.present) {
    return;
  }

  out.type = static_cast<uint8_t>(SD_MMC.cardType());
  switch (SD_MMC.cardType()) {
    case CARD_MMC:
      strncpy(out.type_name, "MMC", sizeof(out.type_name) - 1);
      break;
    case CARD_SD:
      strncpy(out.type_name, "SD", sizeof(out.type_name) - 1);
      break;
    case CARD_SDHC:
      strncpy(out.type_name, "SDHC", sizeof(out.type_name) - 1);
      break;
    default:
      strncpy(out.type_name, "?", sizeof(out.type_name) - 1);
      break;
  }

  out.size_mb = static_cast<uint32_t>(SD_MMC.cardSize() / (1024ULL * 1024ULL));
  out.total_mb = static_cast<uint32_t>(SD_MMC.totalBytes() / (1024ULL * 1024ULL));
  out.used_mb = static_cast<uint32_t>(SD_MMC.usedBytes() / (1024ULL * 1024ULL));
  out.free_mb = (out.total_mb > out.used_mb) ? (out.total_mb - out.used_mb) : 0;

  auto& cfg = ConfigStore::instance();
  cfg.loadFormatFolders();

  out.dirs_ok = true;
  for (size_t i = 0; i < cfg.formatFolderCount(); ++i) {
    const char* dir = cfg.formatFolder(i);
    File f = SD_MMC.open(dir);
    const bool ok = f && f.isDirectory();
    if (f) {
      f.close();
    }
    if (!ok) {
      out.dirs_ok = false;
      break;
    }
  }
}

bool TfExpose::ensureDirs() {
  auto& cfg = ConfigStore::instance();
  cfg.loadFormatFolders();
  for (size_t i = 0; i < cfg.formatFolderCount(); ++i) {
    if (!ensureDirTree(cfg.formatFolder(i))) {
      return false;
    }
  }
  return true;
}

bool TfExpose::checkCard(TfCardInfo& out) {
  out = {};
  // Never mount/unmount under the Files browser — that races SD_MMC mid-list.
  if (exclusive_) {
    fillInfo(out);
    probe_ok_ = out.present;
    probed_ = true;
    last_error_ = out.present ? "" : "no TF card";
    return out.present;
  }
  if (static_cast<int32_t>(msc_cooloff_until_ms_ - millis()) > 0 && !mounted_) {
    last_error_ = "sd_settle";
    out.present = probe_ok_;
    return false;
  }

  const bool keep = mounted_;
  if (!mountCard()) {
    last_error_ = "no TF card";
    probe_ok_ = false;
    probed_ = true;
    return false;
  }

  fillInfo(out);
  probe_ok_ = out.present;
  probed_ = true;
  last_error_ = out.present ? "" : "no TF card";

  PoSer().printf("TF: check type=%s size=%uMB free=%uMB dirs=%s\n", out.type_name, out.size_mb,
                out.free_mb, out.dirs_ok ? "ok" : "miss");

  if (!keep && !enabled_ && !exclusive_) {
    unmountCard();
  }
  return out.present;
}
bool TfExpose::mountCard() {
  if (mounted_) {
    return true;
  }

  // LilyGO-style attempts: 4-bit default, then slower / 1-bit fallbacks
  if (tryBegin(false, SDMMC_FREQ_DEFAULT) || tryBegin(false, 400) || tryBegin(true, 400) ||
      tryBegin(true, SDMMC_FREQ_DEFAULT)) {
    mounted_ = true;
    last_error_ = "";
    return true;
  }

  last_error_ = "no TF card";
  mounted_ = false;
  return false;
}

void TfExpose::unmountCard() {
  if (!mounted_) {
    return;
  }
  SD_MMC.end();
  mounted_ = false;
}


}  // namespace storage
