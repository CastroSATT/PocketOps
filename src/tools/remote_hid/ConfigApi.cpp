#include "tools/remote_hid/ConfigApi.h"

#include <FS.h>
#include <LittleFS.h>
#include <cstring>
#include <vector>

#include "display/Display.h"
#include "led/RgbLed.h"
#include "storage/BootPrefs.h"
#include "storage/ConfigBackup.h"
#include "storage/ConfigStore.h"
#include "storage/DeviceLog.h"
#include "storage/UsbConsole.h"
#include "storage/WifiProfiles.h"
#include "system/ChipTemp.h"
#include "tools/ToolRegistry.h"
#include "tools/remote_hid/HttpCtx.h"
#include "tools/remote_hid/WifiApi.h"
#include "util/JsonEsc.h"

namespace remote_hid {
namespace {

using util::jsonEscape;

struct ConfigRoute {
  const char* id;
  const char* path;
};

constexpr ConfigRoute kConfigRoutes[] = {
    {"network", "/config/pocketops.ini"},
    {"bluetooth", "/config/pocketops_bt.ini"},
    {"mouse", "/config/pocketops_mouse.ini"},
    {"keyboard", "/config/pocketops_keyboard.ini"},
    {"sd", "/config/pocketops_folders.ini"},
    {"ui", "/config/pocketops_ui.ini"},
    {"display", "/config/pocketops_display.ini"},
};

bool ensureParentDir(const char* path) {
  if (!path) {
    return false;
  }
  if (strncmp(path, "/config/", 8) == 0) {
    return LittleFS.exists("/config") || LittleFS.mkdir("/config");
  }
  if (strncmp(path, "/www/", 5) != 0) {
    return true;
  }
  if (!LittleFS.exists("/www") && !LittleFS.mkdir("/www")) {
    return false;
  }
  const char* slash = strrchr(path, '/');
  if (!slash || slash <= path + 4) {
    return true;
  }
  char parent[96];
  const size_t n = static_cast<size_t>(slash - path);
  if (n >= sizeof(parent)) {
    return false;
  }
  memcpy(parent, path, n);
  parent[n] = '\0';
  if (strcmp(parent, "/www") == 0) {
    return true;
  }
  return LittleFS.exists(parent) || LittleFS.mkdir(parent);
}

const char* pathForConfigId(const String& id) {
  for (const auto& r : kConfigRoutes) {
    if (id == r.id) {
      return r.path;
    }
  }
  return nullptr;
}

}  // namespace

bool readTextFile(const char* path, String& out) {
  if (!LittleFS.exists(path)) {
    return false;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    return false;
  }
  out = f.readString();
  f.close();
  return true;
}

bool writeTextFile(const char* path, const String& body) {
  if (!ensureParentDir(path)) {
    return false;
  }
  File f = LittleFS.open(path, "w");
  if (!f) {
    return false;
  }
  const size_t written = f.print(body);
  f.close();
  return written == body.length() || (body.length() == 0 && written == 0);
}

namespace {

String configIdFromUri() {
  // /api/config/<id>
  const String uri = g_http->uri();
  const int base = uri.indexOf("/api/config/");
  if (base < 0) {
    return "";
  }
  String id = uri.substring(base + 12);
  const int q = id.indexOf('?');
  if (q >= 0) {
    id = id.substring(0, q);
  }
  id.trim();
  return id;
}

void handleConfigGet() {
  const String id = configIdFromUri();
  const char* path = pathForConfigId(id);
  if (!path) {
    g_http->send(404, "text/plain", "Unknown config id");
    return;
  }
  String body;
  if (!readTextFile(path, body)) {
    g_http->send(404, "text/plain", String("Missing ") + path);
    return;
  }
  g_http->send(200, "text/plain; charset=utf-8", body);
}

void handleConfigPost() {
  const String id = configIdFromUri();
  const char* path = pathForConfigId(id);
  if (!path) {
    g_http->send(404, "text/plain", "Unknown config id");
    return;
  }
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected raw text body");
    return;
  }
  const String body = g_http->arg("plain");
  if (!writeTextFile(path, body)) {
    g_http->send(500, "text/plain", String("Write failed: ") + path);
    return;
  }
  reloadConfigId(id);
  PoSer().printf("WWW: saved %s (%u bytes)\n", path, static_cast<unsigned>(body.length()));
  PO_LOGI("cfg", "saved %s (%u bytes)", path, static_cast<unsigned>(body.length()));
  g_http->send(200, "text/plain", String("Saved ") + path);
}

std::vector<uint8_t> g_backup_upload;

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
  std::vector<uint8_t> zip;
  if (!storage::ConfigBackup::buildZip(zip) || zip.empty()) {
    g_http->send(500, "text/plain", "Backup failed (no config files?)");
    return;
  }
  g_http->sendHeader("Content-Disposition", "attachment; filename=\"pocketops-config.pobak\"");
  g_http->sendHeader("Cache-Control", "no-store");
  g_http->setContentLength(zip.size());
  g_http->send(200, "application/octet-stream", "");
  g_http->client().write(zip.data(), zip.size());
  PoSer().printf("WWW: backup download %u bytes\n", static_cast<unsigned>(zip.size()));
}

void handleBackupUpload() {
  HTTPUpload& upload = g_http->upload();
  if (upload.status == UPLOAD_FILE_START) {
    g_backup_upload.clear();
    g_backup_upload.reserve(4096);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (g_backup_upload.size() + upload.currentSize > storage::ConfigBackup::kMaxZipBytes) {
      g_backup_upload.clear();
      return;
    }
    g_backup_upload.insert(g_backup_upload.end(), upload.buf, upload.buf + upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    // Finished in handleBackupPost
  }
}

void handleBackupPost() {
  if (g_backup_upload.empty()) {
    g_http->send(400, "text/plain", "Empty upload (use a .pobak backup file)");
    return;
  }
  char err[96];
  const bool ok =
      storage::ConfigBackup::applyZip(g_backup_upload.data(), g_backup_upload.size(), err, sizeof(err));
  g_backup_upload.clear();
  g_backup_upload.shrink_to_fit();
  if (!ok) {
    g_http->send(400, "text/plain", err[0] ? err : "Restore failed");
    return;
  }
  reloadAllConfigsAfterBackup();
  PoSer().println("WWW: config backup restored");
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

void handleDeviceStatusGet() {
  const uint32_t uptime_s = millis() / 1000u;
  float temp_c = 0.0f;
  const bool temp_ok = system_info::chipTempC(temp_c);
  // Live levels (may differ from ini while previewing unsaved slider moves).
  const unsigned bl = display::brightnessPercent();
  const unsigned led = led::RgbLed::instance().brightnessPercent();
  const unsigned sleep_s = display::sleepTimeoutSec();
  char body[192];
  if (temp_ok) {
    snprintf(body, sizeof(body),
             "{\"temp_c\":%.1f,\"temp_ok\":true,\"uptime_s\":%u,\"display_brightness\":%u,"
             "\"light_brightness\":%u,\"display_sleep_sec\":%u,\"asleep\":%s}",
             static_cast<double>(temp_c), static_cast<unsigned>(uptime_s), bl, led, sleep_s,
             display::asleep() ? "true" : "false");
  } else {
    snprintf(body, sizeof(body),
             "{\"temp_c\":null,\"temp_ok\":false,\"uptime_s\":%u,\"display_brightness\":%u,"
             "\"light_brightness\":%u,\"display_sleep_sec\":%u,\"asleep\":%s}",
             static_cast<unsigned>(uptime_s), bl, led, sleep_s,
             display::asleep() ? "true" : "false");
  }
  g_http->send(200, "application/json", body);
}

void handleDevicePreviewPost() {
  // Live preview only — does not write pocketops_display.ini.
  // Body: application/x-www-form-urlencoded keys.
  auto clampPct = [](int v) -> uint8_t {
    if (v < 5) {
      return 5;
    }
    if (v > 100) {
      return 100;
    }
    return static_cast<uint8_t>(v);
  };

  if (g_http->hasArg("display_brightness")) {
    display::setBrightnessPercent(clampPct(g_http->arg("display_brightness").toInt()));
  }
  if (g_http->hasArg("light_brightness")) {
    led::RgbLed::instance().setBrightnessPercent(clampPct(g_http->arg("light_brightness").toInt()));
  }
  if (g_http->hasArg("display_sleep_sec")) {
    long v = g_http->arg("display_sleep_sec").toInt();
    if (v < 0) {
      v = 0;
    }
    if (v > 86400) {
      v = 86400;
    }
    display::setSleepTimeoutSec(static_cast<uint32_t>(v));
  }
  display::noteActivity();
  g_http->send(200, "text/plain", "ok");
}

void handleToolsListGet() {
  auto& reg = tools::ToolRegistry::instance();
  String json = "{\"tools\":[";
  for (int i = 0; i < reg.count(); ++i) {
    tools::Tool* t = reg.at(i);
    if (!t) {
      continue;
    }
    if (i) {
      json += ',';
    }
    char id_esc[40];
    char name_esc[64];
    jsonEscape(t->id(), id_esc, sizeof(id_esc));
    jsonEscape(t->name(), name_esc, sizeof(name_esc));
    json += "{\"id\":\"";
    json += id_esc;
    json += "\",\"name\":\"";
    json += name_esc;
    json += "\"}";
  }
  json += "]}";
  g_http->send(200, "application/json", json);
}

void handleBootToolGet() {
  const char* id = storage::BootPrefs::instance().toolId();
  char id_esc[40];
  jsonEscape(id && id[0] ? id : "menu", id_esc, sizeof(id_esc));
  char body[64];
  snprintf(body, sizeof(body), "{\"id\":\"%s\"}", id_esc);
  g_http->send(200, "application/json", body);
}

void handleBootToolPost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected body: tool id or menu");
    return;
  }
  String body = g_http->arg("plain");
  body.trim();
  String lower = body;
  lower.toLowerCase();
  if (lower.length() && lower != "menu" && lower != "off" && lower != "none" && lower != "-") {
    if (!tools::ToolRegistry::instance().findById(lower.c_str())) {
      g_http->send(400, "text/plain", "Unknown tool id");
      return;
    }
  }
  storage::BootPrefs::instance().setToolId(body.c_str());
  handleBootToolGet();
}


}  // namespace

void registerConfigApiRoutes(WebServer& http) {
  http.on("/api/tools", HTTP_GET, handleToolsListGet);
  http.on("/api/boot/tool", HTTP_GET, handleBootToolGet);
  http.on("/api/boot/tool", HTTP_POST, handleBootToolPost);
  http.on("/api/config/network", HTTP_GET, handleConfigGet);
  http.on("/api/config/network", HTTP_POST, handleConfigPost);
  http.on("/api/config/bluetooth", HTTP_GET, handleConfigGet);
  http.on("/api/config/bluetooth", HTTP_POST, handleConfigPost);
  http.on("/api/config/mouse", HTTP_GET, handleConfigGet);
  http.on("/api/config/mouse", HTTP_POST, handleConfigPost);
  http.on("/api/config/keyboard", HTTP_GET, handleConfigGet);
  http.on("/api/config/keyboard", HTTP_POST, handleConfigPost);
  http.on("/api/config/sd", HTTP_GET, handleConfigGet);
  http.on("/api/config/sd", HTTP_POST, handleConfigPost);
  http.on("/api/config/ui", HTTP_GET, handleConfigGet);
  http.on("/api/config/ui", HTTP_POST, handleConfigPost);
  http.on("/api/config/display", HTTP_GET, handleConfigGet);
  http.on("/api/config/display", HTTP_POST, handleConfigPost);
  http.on("/api/config/backup", HTTP_GET, handleBackupGet);
  http.on("/api/config/backup", HTTP_POST, handleBackupPost, handleBackupUpload);
  http.on("/api/config/factory-reset", HTTP_POST, handleFactoryResetPost);
  http.on("/api/device/status", HTTP_GET, handleDeviceStatusGet);
  http.on("/api/device/preview", HTTP_POST, handleDevicePreviewPost);
}

}  // namespace remote_hid
