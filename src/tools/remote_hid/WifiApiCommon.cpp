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

void sendJsonArena(JsonW& w, char* arena) {
  if (!arena) {
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  if (!w.ok) {
    util::releaseJsonArena(arena);
    g_http->send(500, "application/json", "{\"error\":\"json_overflow\"}");
    return;
  }
  g_http->send(200, "application/json", arena);
  util::releaseJsonArena(arena);
}

void reloadConfigId_impl(const String& id) {
  auto& cfg = storage::ConfigStore::instance();
  if (id == "network") {
    cfg.loadSettings();
    // Web saves must not block on tryJoinKnownNetworks — that scan/join hunt
    // stalls SoftAP clients and leaves the radio unable to list networks.
    // Boot still uses applyWifi(true) in RemoteHidServer::begin().
    applyWifi(false);
  } else if (id == "bluetooth") {
    cfg.loadBluetooth();
  } else if (id == "mouse") {
    cfg.loadMouse();
  } else if (id == "keyboard") {
    cfg.loadKeyboard();
  } else if (id == "sd") {
    cfg.loadFormatFolders();
  } else if (id == "ui") {
    cfg.loadUi();
  } else if (id == "display") {
    cfg.loadDisplay();
    display::applyFromConfig();
    led::RgbLed::instance().setBrightnessPercent(cfg.lightBrightness());
    display::noteActivity();
  }
}

const char* wifiStatusName(wl_status_t st) {
  switch (st) {
    case WL_IDLE_STATUS:
      return "idle";
    case WL_NO_SSID_AVAIL:
      return "no_ssid";
    case WL_SCAN_COMPLETED:
      return "scan_done";
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "failed";
    case WL_CONNECTION_LOST:
      return "lost";
    case WL_DISCONNECTED:
      return "disconnected";
    default:
      return "unknown";
  }
}

void upsertIniKey(String& body, const char* key, const char* value) {
  const String prefix = String(key) + "=";
  String out;
  out.reserve(body.length() + 64);
  bool found = false;
  int start = 0;
  while (start < static_cast<int>(body.length())) {
    int end = body.indexOf('\n', start);
    if (end < 0) {
      end = body.length();
    }
    String line = body.substring(start, end);
    if (line.endsWith("\r")) {
      line.remove(line.length() - 1);
    }
    if (!found && line.startsWith(prefix)) {
      out += prefix;
      out += value;
      found = true;
    } else {
      out += line;
    }
    out += '\n';
    start = end + 1;
  }
  if (!found) {
    out += prefix;
    out += value;
    out += '\n';
  }
  body = out;
}

bool formatWifiStatusJson(char* body, size_t body_len) {
  if (!body || body_len < 64) {
    return false;
  }
  auto& cfg = storage::ConfigStore::instance();
  auto& prof = storage::WifiProfiles::instance();
  prof.load();
  const bool sta = (WiFi.status() == WL_CONNECTED);
  const bool softap = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
  // SoftAP is "fallback" only in join-only mode (STA) when hotspot was opened after join failed.
  // SoftAP-only and SoftAP+join keep the AP on purpose — that is not fallback.
  const bool mode_sta = (strcmp(cfg.wifiMode(), "sta") == 0);
  const bool fallback_ready = mode_sta && softap && !sta;
  char mode_esc[12];
  char ap_esc[40];
  char sta_esc[40];
  char cur_esc[40];
  char bssid_esc[40];
  jsonEscape(cfg.wifiMode(), mode_esc, sizeof(mode_esc));
  jsonEscape(cfg.wifiApSsid(), ap_esc, sizeof(ap_esc));
  jsonEscape(cfg.wifiStaSsid(), sta_esc, sizeof(sta_esc));
  jsonEscape(sta ? WiFi.SSID().c_str() : "", cur_esc, sizeof(cur_esc));
  if (sta) {
    const uint8_t* mac = WiFi.BSSID();
    char raw[18] = {};
    if (mac) {
      snprintf(raw, sizeof(raw), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
               mac[4], mac[5]);
    }
    jsonEscape(raw, bssid_esc, sizeof(bssid_esc));
  } else {
    bssid_esc[0] = '\0';
  }

  const int n = snprintf(
      body, body_len,
      "{\"mode\":\"%s\",\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\",\"ap_up\":%s,"
      "\"sta_ssid\":\"%s\",\"connected_ssid\":\"%s\",\"sta_ip\":\"%s\","
      "\"sta_bssid\":\"%s\",\"sta_connected\":%s,\"rssi\":%d,\"status\":\"%s\","
      "\"profiles\":%u,\"fallback_ready\":%s}",
      mode_esc, ap_esc, WiFi.softAPIP().toString().c_str(), softap ? "true" : "false", sta_esc,
      cur_esc, sta ? WiFi.localIP().toString().c_str() : "0.0.0.0", bssid_esc,
      sta ? "true" : "false", sta ? WiFi.RSSI() : 0, wifiStatusName(WiFi.status()),
      static_cast<unsigned>(prof.count()), fallback_ready ? "true" : "false");
  return n > 0 && static_cast<size_t>(n) < body_len;
}

}  // namespace remote_hid
