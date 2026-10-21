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

void handleFirmwareGet() {
  auto& fw = storage::FirmwareUpdate::instance();
  char body[240];
  snprintf(body, sizeof(body),
           "{\"dfu\":%s,\"armed\":%s,\"upload\":false,"
           "\"note\":\"DFU USB interface arms on next boot when switch is on (default off)\"}",
           fw.dfuEnabled() ? "true" : "false", fw.dfuArmed() ? "true" : "false");
  g_http->send(200, "application/json", body);
}

bool parseFirmwareDfuBody(String body, bool* on_out) {
  body.trim();
  body.toLowerCase();
  if (body == "on" || body == "1" || body == "true") {
    *on_out = true;
    return true;
  }
  if (body == "off" || body == "0" || body == "false") {
    *on_out = false;
    return true;
  }
  return false;
}

void handleFirmwareDfuPost() {
  bool on = false;
  bool ok = false;
  if (g_http->hasArg("plain")) {
    ok = parseFirmwareDfuBody(g_http->arg("plain"), &on);
  } else if (g_http->hasArg("dfu")) {
    ok = parseFirmwareDfuBody(g_http->arg("dfu"), &on);
  }
  if (!ok) {
    g_http->send(400, "text/plain", "Expected body: on or off");
    return;
  }

  auto& fw = storage::FirmwareUpdate::instance();
  const bool was = fw.dfuEnabled();
  fw.setDfuEnabled(on);
  const bool changed = (was != fw.dfuEnabled()) || (fw.dfuEnabled() != fw.dfuArmed());

  char body[280];
  snprintf(body, sizeof(body),
           "{\"dfu\":%s,\"armed\":%s,\"upload\":false,\"reboot\":%s,"
           "\"note\":\"Saved. Rebooting so USB DFU arm matches the switch.\"}",
           fw.dfuEnabled() ? "true" : "false", fw.dfuArmed() ? "true" : "false",
           changed ? "true" : "false");
  g_http->send(200, "application/json", body);

  if (changed) {
    delay(400);
    ESP.restart();
  }
}

void handleFirmwareUploadPost() {
  g_http->send(501, "application/json",
               "{\"error\":\"not_implemented\",\"note\":\"Firmware upload placeholder\"}");
}


void registerFirmwareApiRoutes(WebServer& http) {
  http.on("/api/firmware", HTTP_GET, handleFirmwareGet);
  http.on("/api/firmware/dfu", HTTP_POST, handleFirmwareDfuPost);
  http.on("/api/firmware/upload", HTTP_POST, handleFirmwareUploadPost);
}
}  // namespace remote_hid
