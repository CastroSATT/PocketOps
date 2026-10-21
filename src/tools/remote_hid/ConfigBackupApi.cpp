#include "tools/remote_hid/ConfigApi.h"
#include "tools/remote_hid/ConfigApiInternal.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <cstring>

#include "display/Display.h"
#include "led/RgbLed.h"
#include "storage/BootPrefs.h"
#include "storage/ConfigBackup.h"
#include "storage/ConfigStore.h"
#include "storage/DeviceLog.h"
#include "storage/FirmwareUpdate.h"
#include "storage/UsbConsole.h"
#include "storage/WifiProfiles.h"
#include "system/ChipTemp.h"
#include "tools/ToolRegistry.h"
#include "tools/remote_hid/HttpCtx.h"
#include "tools/remote_hid/WifiApi.h"
#include "util/JsonEsc.h"

namespace remote_hid {

using util::jsonEscape;
using http::g_http;

bool g_backup_upload_ok = false;
size_t g_backup_upload_bytes = 0;

void reloadAllConfigsAfterBackup() {
  reloadConfigId("network");
  reloadConfigId("bluetooth");
  reloadConfigId("mouse");
  reloadConfigId("keyboard");
  reloadConfigId("sd");
  reloadConfigId("ui");
  reloadConfigId("display");
  storage::WifiProfiles::instance().load();
}

void handleBackupGet() {
  using storage::ConfigBackup;
  ConfigBackup::removeTemp(ConfigBackup::kTmpDownload);
  if (!ConfigBackup::buildZipToFile(ConfigBackup::kTmpDownload)) {
    g_http->send(500, "text/plain", "Backup failed (no config files?)");
    return;
  }
  File f = LittleFS.open(ConfigBackup::kTmpDownload, "r");
  if (!f) {
    ConfigBackup::removeTemp(ConfigBackup::kTmpDownload);
    g_http->send(500, "text/plain", "Backup failed (open tmp)");
    return;
  }
  const size_t n = f.size();
  g_http->sendHeader("Content-Disposition", "attachment; filename=\"pocketops-config.pobak\"");
  g_http->sendHeader("Cache-Control", "no-store");
  g_http->setContentLength(n);
  g_http->send(200, "application/octet-stream", "");
  uint8_t buf[512];
  while (f.available()) {
    const size_t got = f.read(buf, sizeof(buf));
    if (!got) {
      break;
    }
    g_http->client().write(buf, got);
    yield();
  }
  f.close();
  ConfigBackup::removeTemp(ConfigBackup::kTmpDownload);
  PoSer().printf("WWW: backup download %u bytes (streamed)\n", static_cast<unsigned>(n));
}

void handleBackupUpload() {
  using storage::ConfigBackup;
  HTTPUpload& upload = g_http->upload();
  if (upload.status == UPLOAD_FILE_START) {
    g_backup_upload_ok = true;
    g_backup_upload_bytes = 0;
    ConfigBackup::removeTemp(ConfigBackup::kTmpUpload);
    File f = LittleFS.open(ConfigBackup::kTmpUpload, "w");
    if (!f) {
      g_backup_upload_ok = false;
    } else {
      f.close();
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!g_backup_upload_ok) {
      return;
    }
    if (g_backup_upload_bytes + upload.currentSize > ConfigBackup::kMaxZipBytes) {
      g_backup_upload_ok = false;
      ConfigBackup::removeTemp(ConfigBackup::kTmpUpload);
      return;
    }
    File f = LittleFS.open(ConfigBackup::kTmpUpload, "a");
    if (!f) {
      g_backup_upload_ok = false;
      return;
    }
    if (f.write(upload.buf, upload.currentSize) != upload.currentSize) {
      g_backup_upload_ok = false;
      f.close();
      ConfigBackup::removeTemp(ConfigBackup::kTmpUpload);
      return;
    }
    g_backup_upload_bytes += upload.currentSize;
    f.close();
  } else if (upload.status == UPLOAD_FILE_END) {
    // Finished in handleBackupPost
  }
}

void handleBackupPost() {
  using storage::ConfigBackup;
  if (!g_backup_upload_ok || g_backup_upload_bytes == 0 ||
      !LittleFS.exists(ConfigBackup::kTmpUpload)) {
    ConfigBackup::removeTemp(ConfigBackup::kTmpUpload);
    g_backup_upload_ok = false;
    g_backup_upload_bytes = 0;
    g_http->send(400, "text/plain", "Empty upload (use a .pobak backup file)");
    return;
  }
  char err[96];
  const bool ok = ConfigBackup::applyZipFromFile(ConfigBackup::kTmpUpload, err, sizeof(err));
  ConfigBackup::removeTemp(ConfigBackup::kTmpUpload);
  g_backup_upload_ok = false;
  g_backup_upload_bytes = 0;
  if (!ok) {
    g_http->send(400, "text/plain", err[0] ? err : "Restore failed");
    return;
  }
  reloadAllConfigsAfterBackup();
  PoSer().println("WWW: config backup restored (streamed)");
  g_http->send(200, "text/plain", "Restored config from backup");
}

bool formArgOn(const char* key, bool default_on) {
  if (!g_http->hasArg(key)) {
    return default_on;
  }
  const String v = g_http->arg(key);
  if (v == "0" || v.equalsIgnoreCase("false") || v.equalsIgnoreCase("off") ||
      v.equalsIgnoreCase("no")) {
    return false;
  }
  if (v == "1" || v.equalsIgnoreCase("true") || v.equalsIgnoreCase("on") ||
      v.equalsIgnoreCase("yes")) {
    return true;
  }
  return default_on;
}

void handleFactoryResetPost() {
  if (!g_http->hasArg("confirm")) {
    g_http->send(400, "text/plain", "Expected confirm=FACTORY RESET");
    return;
  }
  String confirm = g_http->arg("confirm");
  confirm.trim();
  if (confirm != storage::ConfigBackup::kFactoryConfirm) {
    g_http->send(400, "text/plain", "Type FACTORY RESET to confirm");
    return;
  }

  storage::FactoryResetOpts opts;
  opts.settings = formArgOn("settings", true);
  opts.nvs_prefs = formArgOn("nvs", true);
  opts.wifi_profiles = formArgOn("wifi", true);
  opts.lan_profiles = formArgOn("lan", true);
  opts.device_logs = formArgOn("logs", true);

  char err[96];
  if (!storage::ConfigBackup::factoryReset(opts, err, sizeof(err))) {
    g_http->send(400, "text/plain", err[0] ? err : "Factory reset failed");
    return;
  }

  if (opts.settings) {
    reloadAllConfigsAfterBackup();
  } else if (opts.wifi_profiles) {
    storage::WifiProfiles::instance().load();
  }

  PoSer().printf("WWW: factory reset settings=%d nvs=%d wifi=%d lan=%d logs=%d\n",
                 opts.settings ? 1 : 0, opts.nvs_prefs ? 1 : 0, opts.wifi_profiles ? 1 : 0,
                 opts.lan_profiles ? 1 : 0, opts.device_logs ? 1 : 0);
  PO_LOGI("cfg", "factory reset ok");
  g_http->send(200, "text/plain",
               "Factory reset complete. SoftAP may be PocketOps / pocketops — reconnect if needed.");
}


void registerConfigBackupRoutes(WebServer& http) {
  http.on("/api/config/backup", HTTP_GET, handleBackupGet);
  http.on("/api/config/backup", HTTP_POST, handleBackupPost, handleBackupUpload);
  http.on("/api/config/factory-reset", HTTP_POST, handleFactoryResetPost);
}
}  // namespace remote_hid
