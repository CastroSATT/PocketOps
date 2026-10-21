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



void registerDeviceApiRoutes(WebServer& http) {
  http.on("/api/device/status", HTTP_GET, handleDeviceStatusGet);
  http.on("/api/device/preview", HTTP_POST, handleDevicePreviewPost);
  http.on("/api/tools", HTTP_GET, handleToolsListGet);
  http.on("/api/boot/tool", HTTP_GET, handleBootToolGet);
  http.on("/api/boot/tool", HTTP_POST, handleBootToolPost);
}
}  // namespace remote_hid
