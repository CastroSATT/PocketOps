#include "tools/remote_hid/WifiApi.h"

#include <WiFi.h>
#include <cstring>

#include "display/Display.h"
#include "led/RgbLed.h"
#include "storage/ConfigStore.h"
#include "storage/DeviceLog.h"
#include "storage/UsbConsole.h"
#include "storage/WifiProfiles.h"
#include "tools/remote_hid/ConfigApi.h"
#include "tools/remote_hid/HttpCtx.h"
#include "tools/remote_hid/WifiInternal.h"
#include "util/JsonArenaPool.h"
#include "util/JsonEsc.h"
#include "util/JsonWriter.h"

namespace remote_hid {

using util::jsonEscape;
using util::JsonW;

void handleWifiProfilesGet() {
  auto& prof = storage::WifiProfiles::instance();
  prof.load();
  constexpr size_t kCap = 12288;
  char* arena = util::acquireJsonArena(kCap);
  if (!arena) {
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  JsonW w;
  w.begin(arena, kCap);
  w.put("{\"profiles\":[");
  for (size_t i = 0; i < prof.count(); ++i) {
    if (i) {
      w.putc(',');
    }
    char ssid_esc[68];
    char pass_esc[140];
    char bssid_esc[40];
    jsonEscape(prof.ssid(i), ssid_esc, sizeof(ssid_esc));
    jsonEscape(prof.pass(i), pass_esc, sizeof(pass_esc));
    jsonEscape(prof.bssid(i), bssid_esc, sizeof(bssid_esc));
    w.put("{\"ssid\":\"");
    w.put(ssid_esc);
    w.put("\",\"pass\":\"");
    w.put(pass_esc);
    w.put("\",\"bssid\":\"");
    w.put(bssid_esc);
    w.put("\",\"auto_join\":");
    w.putBool(prof.autoJoin(i));
    w.putc('}');
  }
  w.put("]}");
  sendJsonArena(w, arena);
}

void handleWifiProfilesDeletePost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected ssid=");
    return;
  }
  String ssid;
  String body = g_http->arg("plain");
  int start = 0;
  while (start <= static_cast<int>(body.length())) {
    int end = body.indexOf('\n', start);
    if (end < 0) {
      end = body.length();
    }
    String line = body.substring(start, end);
    line.trim();
    if (line.startsWith("ssid=")) {
      ssid = line.substring(5);
      ssid.trim();
    }
    if (end >= static_cast<int>(body.length())) {
      break;
    }
    start = end + 1;
  }
  if (!ssid.length()) {
    g_http->send(400, "text/plain", "Missing ssid=");
    return;
  }
  auto& prof = storage::WifiProfiles::instance();
  prof.load();
  if (!prof.remove(ssid.c_str())) {
    g_http->send(404, "text/plain", "Profile not found");
    return;
  }
  PoSer().printf("WWW: WiFi profile deleted %s\n", ssid.c_str());
  handleWifiProfilesGet();
}

void handleWifiProfilesUpdatePost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected ssid= / pass= / bssid= / auto_join=");
    return;
  }
  String ssid;
  String pass;
  String bssid;
  bool have_pass = false;
  bool have_auto = false;
  bool auto_join = true;
  String body = g_http->arg("plain");
  int start = 0;
  while (start <= static_cast<int>(body.length())) {
    int end = body.indexOf('\n', start);
    if (end < 0) {
      end = body.length();
    }
    String line = body.substring(start, end);
    line.trim();
    if (line.startsWith("ssid=")) {
      ssid = line.substring(5);
      ssid.trim();
    } else if (line.startsWith("pass=")) {
      pass = line.substring(5);
      have_pass = true;
    } else if (line.startsWith("bssid=")) {
      bssid = line.substring(6);
      bssid.trim();
    } else if (line.startsWith("auto_join=")) {
      String v = line.substring(10);
      v.trim();
      auto_join = !(v == "0" || v.equalsIgnoreCase("false") || v.equalsIgnoreCase("no"));
      have_auto = true;
    }
    if (end >= static_cast<int>(body.length())) {
      break;
    }
    start = end + 1;
  }
  if (!ssid.length()) {
    g_http->send(400, "text/plain", "Missing ssid=");
    return;
  }
  auto& prof = storage::WifiProfiles::instance();
  prof.load();
  if (!prof.hasSsid(ssid.c_str())) {
    g_http->send(404, "text/plain", "Profile not found");
    return;
  }
  char old_pass[65] = {};
  if (!have_pass) {
    prof.findPass(ssid.c_str(), old_pass, sizeof(old_pass));
    pass = old_pass;
  }
  if (!prof.upsert(ssid.c_str(), pass.c_str(), bssid.length() ? bssid.c_str() : nullptr, have_auto,
                   auto_join)) {
    g_http->send(500, "text/plain", "Update failed");
    return;
  }
  PoSer().printf("WWW: WiFi profile updated %s\n", ssid.c_str());
  handleWifiProfilesGet();
}


void registerWifiProfileRoutes(WebServer& http) {
  http.on("/api/wifi/profiles", HTTP_GET, handleWifiProfilesGet);
  http.on("/api/wifi/profiles/delete", HTTP_POST, handleWifiProfilesDeletePost);
  http.on("/api/wifi/profiles/update", HTTP_POST, handleWifiProfilesUpdatePost);
}
}  // namespace remote_hid
