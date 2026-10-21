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


void registerConfigCrudRoutes(WebServer& http) {
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
}
}  // namespace remote_hid
