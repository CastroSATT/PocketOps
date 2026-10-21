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
#include "http/ChromeApi.h"
#include "tools/remote_hid/WifiInternal.h"
#include "util/JsonArenaPool.h"
#include "util/JsonEsc.h"
#include "util/JsonWriter.h"

namespace remote_hid {

using util::jsonEscape;
using util::JsonW;

void handleWifiScanGet() {
  auto& prof = storage::WifiProfiles::instance();
  prof.load();

  const wifi_mode_t prev = WiFi.getMode();
  const bool sta_was_up = (WiFi.status() == WL_CONNECTED);
  if (prev == WIFI_AP) {
    WiFi.mode(WIFI_AP_STA);
    delay(120);
  } else if (prev == WIFI_OFF) {
    WiFi.mode(WIFI_STA);
    delay(80);
  }

  // Active STA reconnect can make scanNetworks return -1 / empty.
  WiFi.setAutoReconnect(false);
  if (!sta_was_up) {
    WiFi.disconnect(false, false);
    delay(40);
  }

  int n = WiFi.scanNetworks(false, false);
  if (n < 0) {
    delay(200);
    n = WiFi.scanNetworks(false, false);
  }
  if (n < 0) {
    n = 0;
  }

  constexpr size_t kCap = 16384;
  char* arena = util::acquireJsonArena(kCap);
  JsonW w;
  if (arena) {
    w.begin(arena, kCap);
    w.put("{\"networks\":[");
    for (int i = 0; i < n; ++i) {
      if (i) {
        w.putc(',');
      }
      const String ssid = WiFi.SSID(i);
      char ssid_esc[68];
      jsonEscape(ssid.c_str(), ssid_esc, sizeof(ssid_esc));
      w.put("{\"ssid\":\"");
      w.put(ssid_esc);
      w.put("\",\"rssi\":");
      w.puti(WiFi.RSSI(i));
      w.put(",\"secure\":");
      w.putBool(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      w.put(",\"known\":");
      w.putBool(prof.hasSsid(ssid.c_str()));
      w.put(",\"auto_join\":");
      w.putBool(prof.isAutoJoin(ssid.c_str()));
      w.put(",\"bssid\":\"");
      char bssid_esc[40];
      jsonEscape(WiFi.BSSIDstr(i).c_str(), bssid_esc, sizeof(bssid_esc));
      w.put(bssid_esc);
      w.put("\"}");
    }
    w.put("]}");
  }
  WiFi.scanDelete();

  auto& cfg = storage::ConfigStore::instance();
  const bool want_sta =
      (strcmp(cfg.wifiMode(), "sta") == 0) || (strcmp(cfg.wifiMode(), "apsta") == 0);
  WiFi.setAutoReconnect(want_sta);

  if (prev == WIFI_AP) {
    WiFi.mode(WIFI_AP);
    startSoftAp();
  } else if (sta_was_up && WiFi.status() != WL_CONNECTED && cfg.wifiStaSsid()[0]) {
    WiFi.begin(cfg.wifiStaSsid(), cfg.wifiStaPass());
  }

  sendJsonArena(w, arena);
}

void handleWifiConnectPost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected ssid=... and pass=...");
    return;
  }

  String ssid;
  String pass;
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

  // If password omitted, reuse a saved profile password.
  auto& prof = storage::WifiProfiles::instance();
  prof.load();
  if (!pass.length()) {
    char saved[65] = {};
    if (prof.findPass(ssid.c_str(), saved, sizeof(saved))) {
      pass = saved;
    }
  }

  auto& cfg = storage::ConfigStore::instance();
  String ini;
  if (!readTextFile("/config/pocketops.ini", ini)) {
    g_http->send(500, "text/plain", "Missing pocketops.ini");
    return;
  }
  // Keep SoftAP + join so the page remains reachable if LAN drops.
  upsertIniKey(ini, "wifi_mode", "apsta");
  upsertIniKey(ini, "wifi_sta_ssid", ssid.c_str());
  upsertIniKey(ini, "wifi_sta_pass", pass.c_str());
  if (!writeTextFile("/config/pocketops.ini", ini)) {
    g_http->send(500, "text/plain", "Failed to save credentials");
    return;
  }

  cfg.loadSettings();
  PoSer().printf("WWW: WiFi join %s\n", ssid.c_str());

  // Direct join of the chosen network (faster than full multi-hunt).
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  startSoftAp();
  WiFi.disconnect(false, false);
  delay(30);
  WiFi.begin(ssid.c_str(), pass.c_str());
  const bool ok = waitForSta(15000);
  if (!ok) {
    const char* st = wifiStatusName(WiFi.status());
    const uint8_t reason = g_sta_disc_reason;
    const char* code = "join_failed";
    const char* msg = "Join failed";
    if (isWrongPasswordReason(reason) || strcmp(st, "failed") == 0) {
      code = "wrong_password";
      msg = "Wrong password";
    } else if (reason == WIFI_REASON_NO_AP_FOUND || strcmp(st, "no_ssid") == 0) {
      code = "not_found";
      msg = "Network not found";
    } else if (strcmp(st, "disconnected") == 0 || strcmp(st, "idle") == 0) {
      // Still disconnected with no auth reason → treat as timeout.
      code = "timeout";
      msg = "Timed out";
    } else if (strcmp(st, "lost") == 0) {
      code = "lost";
      msg = "Connection lost";
    }
    PoSer().printf("WWW: WiFi join fail status=%s reason=%u → %s\n", st,
                  static_cast<unsigned>(reason), code);
    char err[240];
    snprintf(err, sizeof(err),
             "{\"ok\":false,\"status\":\"%s\",\"error\":\"%s\",\"message\":\"%s\","
             "\"reason\":%u,\"fallback_ready\":true}",
             st, code, msg, static_cast<unsigned>(reason));
    g_http->send(500, "application/json", err);
    return;
  }

  // Remember successful venue for next auto-join (include AP MAC when known).
  char bssid[18] = {};
  const uint8_t* mac = WiFi.BSSID();
  if (mac) {
    snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
  }
  if (!prof.upsert(ssid.c_str(), pass.c_str(), bssid[0] ? bssid : nullptr)) {
    PoSer().println("WIFI: warn — could not store profile");
  }
  http::sendChromeStatus();
}



void registerWifiScanRoutes(WebServer& http) {
  http.on("/api/wifi/scan", HTTP_GET, handleWifiScanGet);
  http.on("/api/wifi/connect", HTTP_POST, handleWifiConnectPost);
}
}  // namespace remote_hid
