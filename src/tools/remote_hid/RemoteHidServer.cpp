#include "tools/remote_hid/RemoteHidServer.h"

#include <FS.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <cstring>

#include "Branding.h"
#include "display/Display.h"
#include "generated/RemoteHidWwwSeed.h"
#include "hid/HidProtocol.h"
#include "hid/UsbHid.h"
#include "led/RgbLed.h"
#include "storage/ConfigBackup.h"
#include "storage/ConfigStore.h"
#include "storage/TfExpose.h"
#include "storage/WifiProfiles.h"
#include "system/ChipTemp.h"

#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

WebServer* g_http = nullptr;
WebSocketsServer* g_ws = nullptr;
int g_http_port = -1;
int g_ws_port = -1;
remote_hid::RemoteHidServer* g_server = nullptr;

constexpr const char* kPagePath = "/www/index.html";

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

void jsonEscape(const char* in, char* out, size_t out_len);
bool readTextFile(const char* path, String& out);
bool writeTextFile(const char* path, const String& body);

const char* pathForConfigId(const String& id) {
  for (const auto& r : kConfigRoutes) {
    if (id == r.id) {
      return r.path;
    }
  }
  return nullptr;
}

uint8_t g_sta_disc_reason = 0;

void ensureWifiStaEvents() {
  static bool once = false;
  if (once) {
    return;
  }
  once = true;
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      g_sta_disc_reason = info.wifi_sta_disconnected.reason;
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      g_sta_disc_reason = 0;
    }
  });
}

bool isWrongPasswordReason(uint8_t reason) {
  // Wrong PSK usually shows up as 4-way / handshake timeout, not WL_CONNECT_FAILED.
  return reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
         reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_AUTH_FAIL ||
         reason == WIFI_REASON_MIC_FAILURE || reason == WIFI_REASON_802_1X_AUTH_FAILED ||
         reason == WIFI_REASON_AUTH_EXPIRE || reason == WIFI_REASON_PAIRWISE_CIPHER_INVALID ||
         reason == WIFI_REASON_GROUP_CIPHER_INVALID || reason == WIFI_REASON_IE_IN_4WAY_DIFFERS;
}

bool waitForSta(uint32_t timeout_ms) {
  ensureWifiStaEvents();
  g_sta_disc_reason = 0;
  const uint32_t start = millis();
  while ((millis() - start) < timeout_ms) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    const uint8_t reason = g_sta_disc_reason;
    if (reason == WIFI_REASON_NO_AP_FOUND) {
      return false;
    }
    if (isWrongPasswordReason(reason)) {
      // Brief grace for ESP retries, then fail as wrong password instead of full timeout.
      const uint32_t grace_end = millis() + 2500;
      while (millis() < grace_end && (millis() - start) < timeout_ms) {
        if (WiFi.status() == WL_CONNECTED) {
          return true;
        }
        delay(40);
        yield();
      }
      WiFi.disconnect(false, false);
      return false;
    }
    delay(40);
    yield();
  }
  return WiFi.status() == WL_CONNECTED;
}

void startSoftAp() {
  auto& cfg = storage::ConfigStore::instance();
  WiFi.softAP(cfg.wifiApSsid(), cfg.wifiApPass(), cfg.wifiApChannel());
  Serial.printf("WIFI: SoftAP %s ch=%d ip=%s\n", cfg.wifiApSsid(), cfg.wifiApChannel(),
                WiFi.softAPIP().toString().c_str());
}

// Scan and try known profiles (strongest first). Also tries last STA from pocketops.ini.
bool tryJoinKnownNetworks(uint32_t per_net_ms) {
  auto& cfg = storage::ConfigStore::instance();
  auto& prof = storage::WifiProfiles::instance();
  prof.load();

  struct Cand {
    int rssi;
    char ssid[33];
    char pass[65];
  };
  // Cap stack candidates (in-range matches); profile store itself holds up to 100.
  constexpr size_t kMaxCands = 24;
  Cand cands[kMaxCands];
  size_t nc = 0;

  auto addCand = [&](const char* ssid, const char* pass, int rssi) {
    if (!ssid || !ssid[0] || nc >= (sizeof(cands) / sizeof(cands[0]))) {
      return;
    }
    for (size_t i = 0; i < nc; ++i) {
      if (strcmp(cands[i].ssid, ssid) == 0) {
        if (rssi > cands[i].rssi) {
          cands[i].rssi = rssi;
        }
        return;
      }
    }
    strncpy(cands[nc].ssid, ssid, sizeof(cands[nc].ssid) - 1);
    cands[nc].ssid[sizeof(cands[nc].ssid) - 1] = '\0';
    strncpy(cands[nc].pass, pass ? pass : "", sizeof(cands[nc].pass) - 1);
    cands[nc].pass[sizeof(cands[nc].pass) - 1] = '\0';
    cands[nc].rssi = rssi;
    ++nc;
  };

  const int n = WiFi.scanNetworks(false, false);
  for (int i = 0; i < n; ++i) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.length()) {
      continue;
    }
    char pass[65] = {};
    if (prof.isAutoJoin(ssid.c_str()) && prof.findPass(ssid.c_str(), pass, sizeof(pass))) {
      addCand(ssid.c_str(), pass, WiFi.RSSI(i));
    } else if (cfg.wifiStaSsid()[0] && ssid == cfg.wifiStaSsid()) {
      // Last-joined network always eligible even if profile auto_join is off.
      addCand(cfg.wifiStaSsid(), cfg.wifiStaPass(), WiFi.RSSI(i));
    }
  }
  WiFi.scanDelete();

  // If scan found nothing known, still try last-used STA + auto-join profiles.
  if (nc == 0) {
    if (cfg.wifiStaSsid()[0]) {
      addCand(cfg.wifiStaSsid(), cfg.wifiStaPass(), -100);
    }
    for (size_t i = 0; i < prof.count(); ++i) {
      if (!prof.autoJoin(i)) {
        continue;
      }
      addCand(prof.ssid(i), prof.pass(i), -120 - static_cast<int>(i));
    }
  }

  // Strongest first.
  for (size_t i = 0; i + 1 < nc; ++i) {
    for (size_t j = i + 1; j < nc; ++j) {
      if (cands[j].rssi > cands[i].rssi) {
        Cand tmp = cands[i];
        cands[i] = cands[j];
        cands[j] = tmp;
      }
    }
  }

  for (size_t i = 0; i < nc; ++i) {
    Serial.printf("WIFI: try %s (rssi=%d)\n", cands[i].ssid, cands[i].rssi);
    WiFi.disconnect(false, false);
    delay(30);
    WiFi.begin(cands[i].ssid, cands[i].pass);
    if (waitForSta(per_net_ms)) {
      Serial.printf("WIFI: STA ok %s ip=%s rssi=%d\n", cands[i].ssid,
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      return true;
    }
    Serial.printf("WIFI: fail %s\n", cands[i].ssid);
  }
  return false;
}

void applyWifi(bool wait_for_sta) {
  auto& cfg = storage::ConfigStore::instance();
  auto& prof = storage::WifiProfiles::instance();
  prof.load();

  const char* mode = cfg.wifiMode();
  if (!mode || !mode[0]) {
    mode = "ap";
  }

  const bool mode_ap = (strcmp(mode, "ap") == 0);
  const bool mode_sta = (strcmp(mode, "sta") == 0);
  const bool mode_apsta = (strcmp(mode, "apsta") == 0);
  const bool want_sta = mode_sta || mode_apsta;
  // SoftAP stays up for ap/apsta, and as fallback when sta-only join fails.
  bool want_ap = mode_ap || mode_apsta;

  WiFi.persistent(false);
  WiFi.setAutoReconnect(want_sta);
  WiFi.disconnect(false, false);
  delay(40);

  if (want_ap && want_sta) {
    WiFi.mode(WIFI_AP_STA);
  } else if (want_sta) {
    WiFi.mode(WIFI_STA);
  } else {
    WiFi.mode(WIFI_AP);
  }

  if (want_ap) {
    startSoftAp();
  } else {
    WiFi.softAPdisconnect(true);
  }

  bool joined = false;
  if (want_sta && wait_for_sta) {
    if (want_ap) {
      // Already AP_STA — SoftAP stays while we hunt known networks.
      joined = tryJoinKnownNetworks(8000);
    } else {
      joined = tryJoinKnownNetworks(8000);
      if (!joined) {
        // Fallback: open SoftAP so the page is still reachable for setup.
        Serial.println("WIFI: STA failed — SoftAP fallback");
        WiFi.mode(WIFI_AP_STA);
        startSoftAp();
        want_ap = true;
      }
    }
  } else if (want_sta && !wait_for_sta && cfg.wifiStaSsid()[0]) {
    WiFi.begin(cfg.wifiStaSsid(), cfg.wifiStaPass());
  }

  if (want_sta && wait_for_sta && !joined) {
    Serial.println("WIFI: no known network joined (SoftAP available)");
  }
}

void reloadConfigId(const String& id) {
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

void sendWifiStatus() {
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

  char body[560];
  snprintf(body, sizeof(body),
           "{\"mode\":\"%s\",\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\",\"ap_up\":%s,"
           "\"sta_ssid\":\"%s\",\"connected_ssid\":\"%s\",\"sta_ip\":\"%s\","
           "\"sta_bssid\":\"%s\",\"sta_connected\":%s,\"rssi\":%d,\"status\":\"%s\","
           "\"profiles\":%u,\"fallback_ready\":%s}",
           mode_esc, ap_esc, WiFi.softAPIP().toString().c_str(), softap ? "true" : "false",
           sta_esc, cur_esc, sta ? WiFi.localIP().toString().c_str() : "0.0.0.0", bssid_esc,
           sta ? "true" : "false", sta ? WiFi.RSSI() : 0, wifiStatusName(WiFi.status()),
           static_cast<unsigned>(prof.count()), fallback_ready ? "true" : "false");
  g_http->send(200, "application/json", body);
}

void handleWifiStatusGet() { sendWifiStatus(); }

void handleWifiProfilesGet() {
  auto& prof = storage::WifiProfiles::instance();
  prof.load();
  String json = "{\"profiles\":[";
  for (size_t i = 0; i < prof.count(); ++i) {
    if (i) {
      json += ',';
    }
    char ssid_esc[68];
    char pass_esc[140];
    char bssid_esc[40];
    jsonEscape(prof.ssid(i), ssid_esc, sizeof(ssid_esc));
    jsonEscape(prof.pass(i), pass_esc, sizeof(pass_esc));
    jsonEscape(prof.bssid(i), bssid_esc, sizeof(bssid_esc));
    json += "{\"ssid\":\"";
    json += ssid_esc;
    json += "\",\"pass\":\"";
    json += pass_esc;
    json += "\",\"bssid\":\"";
    json += bssid_esc;
    json += "\",\"auto_join\":";
    json += prof.autoJoin(i) ? "true" : "false";
    json += '}';
  }
  json += "]}";
  g_http->send(200, "application/json", json);
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
  Serial.printf("WWW: WiFi profile deleted %s\n", ssid.c_str());
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
  Serial.printf("WWW: WiFi profile updated %s\n", ssid.c_str());
  handleWifiProfilesGet();
}

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

  String json = "{\"networks\":[";
  for (int i = 0; i < n; ++i) {
    if (i) {
      json += ',';
    }
    const String ssid = WiFi.SSID(i);
    char ssid_esc[68];
    jsonEscape(ssid.c_str(), ssid_esc, sizeof(ssid_esc));
    json += "{\"ssid\":\"";
    json += ssid_esc;
    json += "\",\"rssi\":";
    json += String(WiFi.RSSI(i));
    json += ",\"secure\":";
    json += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false";
    json += ",\"known\":";
    json += prof.hasSsid(ssid.c_str()) ? "true" : "false";
    json += ",\"auto_join\":";
    json += prof.isAutoJoin(ssid.c_str()) ? "true" : "false";
    json += ",\"bssid\":\"";
    {
      char bssid_esc[40];
      jsonEscape(WiFi.BSSIDstr(i).c_str(), bssid_esc, sizeof(bssid_esc));
      json += bssid_esc;
    }
    json += "\"}";
  }
  json += "]}";
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

  g_http->send(200, "application/json", json);
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
  Serial.printf("WWW: WiFi join %s\n", ssid.c_str());

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
    Serial.printf("WWW: WiFi join fail status=%s reason=%u → %s\n", st,
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
    Serial.println("WIFI: warn — could not store profile");
  }
  sendWifiStatus();
}

bool ensureDir(const char* path) {
  if (LittleFS.exists(path)) {
    File f = LittleFS.open(path);
    const bool is_dir = f && f.isDirectory();
    if (f) {
      f.close();
    }
    if (is_dir) {
      return true;
    }
    LittleFS.remove(path);
  }
  return LittleFS.mkdir(path);
}

bool ensureWwwDir() { return ensureDir("/www"); }

bool ensureWwwParents(const char* fs_path) {
  // fs_path like /www/css/app.css → mkdir /www and /www/css
  if (!fs_path || strncmp(fs_path, "/www/", 5) != 0) {
    return false;
  }
  if (!ensureWwwDir()) {
    return false;
  }
  const char* slash = strrchr(fs_path, '/');
  if (!slash || slash <= fs_path + 4) {
    return true;
  }
  char parent[96];
  const size_t n = static_cast<size_t>(slash - fs_path);
  if (n >= sizeof(parent)) {
    return false;
  }
  memcpy(parent, fs_path, n);
  parent[n] = '\0';
  if (strcmp(parent, "/www") == 0) {
    return true;
  }
  // One nesting level under /www (css|js) is enough for this UI.
  return ensureDir(parent);
}

bool seedWwwFile(const WwwSeedFile& file) {
  if (LittleFS.exists(file.path)) {
    return true;
  }
  if (!ensureWwwParents(file.path)) {
    Serial.printf("WWW: mkdir parents fail %s\n", file.path);
    return false;
  }
  File f = LittleFS.open(file.path, "w");
  if (!f) {
    Serial.printf("WWW: create fail %s\n", file.path);
    return false;
  }
  f.print(FPSTR(file.data));
  const size_t n = f.size();
  f.close();
  Serial.printf("WWW: seeded %s (%u bytes)\n", file.path, static_cast<unsigned>(n));
  return true;
}

bool seedWwwIfMissing() {
  bool ok = true;
  for (size_t i = 0; i < WWW_SEED_FILE_COUNT; ++i) {
    if (!seedWwwFile(WWW_SEED_FILES[i])) {
      ok = false;
    }
  }
  return ok;
}

const char* mimeForWwwPath(const char* path) {
  if (!path) {
    return "text/plain";
  }
  if (strstr(path, ".css")) {
    return "text/css";
  }
  if (strstr(path, ".js")) {
    return "application/javascript";
  }
  if (strstr(path, ".html")) {
    return "text/html";
  }
  return "text/plain";
}

bool wwwAssetUriToFsPath(const String& uri, char* out, size_t out_len) {
  // Allow only /css/* and /js/* → /www/css/* and /www/js/*
  if (!(uri.startsWith("/css/") || uri.startsWith("/js/"))) {
    return false;
  }
  if (uri.indexOf("..") >= 0 || uri.indexOf('\\') >= 0) {
    return false;
  }
  for (size_t i = 0; i < uri.length(); ++i) {
    const char c = uri[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '/' || c == '.' || c == '_' || c == '-';
    if (!ok) {
      return false;
    }
  }
  const size_t need = 5 + uri.length();  // "/www" + uri + NUL
  if (need >= out_len) {
    return false;
  }
  snprintf(out, out_len, "/www%s", uri.c_str());
  return true;
}

void handleWwwAssetGet() {
  char fs_path[96];
  if (!wwwAssetUriToFsPath(g_http->uri(), fs_path, sizeof(fs_path))) {
    g_http->send(404, "text/plain", "Not found");
    return;
  }
  seedWwwIfMissing();
  if (!LittleFS.exists(fs_path)) {
    g_http->send(404, "text/plain", "Not found");
    return;
  }
  File f = LittleFS.open(fs_path, "r");
  if (!f) {
    g_http->send(500, "text/plain", "Open failed");
    return;
  }
  g_http->streamFile(f, mimeForWwwPath(fs_path));
  f.close();
}

bool loadPage(String& out) {
  if (!seedWwwIfMissing()) {
    return false;
  }
  File f = LittleFS.open(kPagePath, "r");
  if (!f) {
    Serial.printf("WWW: open fail %s\n", kPagePath);
    return false;
  }
  out = f.readString();
  f.close();
  if (out.length() == 0) {
    Serial.println("WWW: empty page — re-seeding");
    LittleFS.remove(kPagePath);
    // Force re-seed of index only.
    for (size_t i = 0; i < WWW_SEED_FILE_COUNT; ++i) {
      if (strcmp(WWW_SEED_FILES[i].path, kPagePath) == 0) {
        if (!seedWwwFile(WWW_SEED_FILES[i])) {
          return false;
        }
        break;
      }
    }
    f = LittleFS.open(kPagePath, "r");
    if (!f) {
      return false;
    }
    out = f.readString();
    f.close();
  }
  return out.length() > 0;
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
  // Ensure parent dir for /config/... or /www/...
  if (strncmp(path, "/www/", 5) == 0) {
    if (!ensureWwwParents(path)) {
      return false;
    }
  } else if (strncmp(path, "/config/", 8) == 0) {
    if (!LittleFS.exists("/config") && !LittleFS.mkdir("/config")) {
      return false;
    }
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
  Serial.printf("WWW: saved %s (%u bytes)\n", path, static_cast<unsigned>(body.length()));
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
  Serial.printf("WWW: backup download %u bytes\n", static_cast<unsigned>(zip.size()));
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
  Serial.println("WWW: config backup restored");
  g_http->send(200, "text/plain", "Restored config from backup");
}

void jsonEscape(const char* in, char* out, size_t out_len) {
  if (!out || out_len == 0) {
    return;
  }
  size_t o = 0;
  if (!in) {
    out[0] = '\0';
    return;
  }
  for (size_t i = 0; in[i] && o + 2 < out_len; ++i) {
    const char c = in[i];
    if (c == '"' || c == '\\') {
      out[o++] = '\\';
      out[o++] = c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      continue;
    } else {
      out[o++] = c;
    }
  }
  out[o] = '\0';
}

void handleSdIconGet() {
  // Lightweight — no mount. Powers the header badge flash like the LCD icon.
  auto& tf = storage::TfExpose::instance();
  const bool expose = tf.enabled();
  const bool activity = expose && tf.activityRecent();
  char body[64];
  snprintf(body, sizeof(body), "{\"expose\":%s,\"activity\":%s}", expose ? "true" : "false",
           activity ? "true" : "false");
  g_http->send(200, "application/json", body);
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

void handleSdStatusGet() {
  auto& tf = storage::TfExpose::instance();
  auto& cfg = storage::ConfigStore::instance();
  cfg.loadFormatFolders();

  storage::TfCardInfo info;
  const bool present = tf.checkCard(info);
  const char* err = tf.lastError();

  char type_esc[16];
  char label_esc[24];
  char rev_esc[16];
  char err_esc[48];
  jsonEscape(info.type_name, type_esc, sizeof(type_esc));
  jsonEscape(cfg.volumeLabel(), label_esc, sizeof(label_esc));
  jsonEscape(branding::kFirmwareVersion, rev_esc, sizeof(rev_esc));
  jsonEscape(err && err[0] ? err : "", err_esc, sizeof(err_esc));

  char body[512];
  snprintf(body, sizeof(body),
           "{\"expose\":%s,\"mounted\":%s,\"present\":%s,\"type\":\"%s\",\"size_mb\":%u,"
           "\"total_mb\":%u,\"used_mb\":%u,\"free_mb\":%u,\"dirs_ok\":%s,"
           "\"volume_label\":\"%s\",\"usb_vendor\":\"PocketOps\",\"usb_product\":\"%s\","
           "\"usb_revision\":\"%s\",\"error\":\"%s\",\"activity\":%s}",
           tf.enabled() ? "true" : "false", tf.mounted() ? "true" : "false",
           present ? "true" : "false", type_esc, static_cast<unsigned>(info.size_mb),
           static_cast<unsigned>(info.total_mb), static_cast<unsigned>(info.used_mb),
           static_cast<unsigned>(info.free_mb), info.dirs_ok ? "true" : "false", label_esc,
           label_esc, rev_esc, err_esc, (tf.enabled() && tf.activityRecent()) ? "true" : "false");
  g_http->send(200, "application/json", body);
}

void handleSdExposePost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected body: on or off");
    return;
  }
  String body = g_http->arg("plain");
  body.trim();
  body.toLowerCase();

  auto& tf = storage::TfExpose::instance();
  if (body == "on" || body == "1" || body == "true") {
    Serial.println("WWW: Expose on");
    if (!tf.enable()) {
      const char* err = tf.lastError();
      g_http->send(500, "text/plain", err && err[0] ? err : "expose enable failed");
      return;
    }
  } else if (body == "off" || body == "0" || body == "false") {
    Serial.println("WWW: Expose off");
    tf.disable();
  } else {
    g_http->send(400, "text/plain", "Expected body: on or off");
    return;
  }
  handleSdStatusGet();
}

struct SdFormatJob {
  volatile bool busy = false;
  volatile bool done = false;
  volatile bool ok = false;
  volatile int percent = 0;
  char status[48] = {};
  char message[96] = {};
} g_sd_fmt;

void setSdFormatProgress(int percent, const char* status) {
  if (percent < 0) {
    percent = 0;
  }
  if (percent > 100) {
    percent = 100;
  }
  g_sd_fmt.percent = percent;
  if (!status) {
    return;
  }
  const char* line = status;
  if (strncmp(status, "TF: ", 4) == 0) {
    line = status + 4;
  }
  strncpy(g_sd_fmt.status, line, sizeof(g_sd_fmt.status) - 1);
  g_sd_fmt.status[sizeof(g_sd_fmt.status) - 1] = '\0';
}

void sdFormatTask(void* /*arg*/) {
  storage::TfCardInfo info;
  setSdFormatProgress(0, "starting");
  const bool ok = storage::TfExpose::instance().formatPocketLayout(
      info, [](int p, const char* s) { setSdFormatProgress(p, s); });
  g_sd_fmt.ok = ok;
  if (ok) {
    snprintf(g_sd_fmt.message, sizeof(g_sd_fmt.message), "Format OK %uMB label=%s",
             static_cast<unsigned>(info.size_mb),
             storage::ConfigStore::instance().volumeLabel());
    setSdFormatProgress(100, "format OK");
  } else {
    const char* err = storage::TfExpose::instance().lastError();
    strncpy(g_sd_fmt.message, err && err[0] ? err : "format fail", sizeof(g_sd_fmt.message) - 1);
    g_sd_fmt.message[sizeof(g_sd_fmt.message) - 1] = '\0';
  }
  g_sd_fmt.done = true;
  g_sd_fmt.busy = false;
  vTaskDelete(nullptr);
}

void handleSdFormatProgressGet() {
  char status_esc[96];
  char msg_esc[160];
  jsonEscape(g_sd_fmt.status, status_esc, sizeof(status_esc));
  jsonEscape(g_sd_fmt.message, msg_esc, sizeof(msg_esc));
  char body[320];
  snprintf(body, sizeof(body),
           "{\"busy\":%s,\"done\":%s,\"ok\":%s,\"percent\":%d,\"status\":\"%s\",\"message\":\"%s\"}",
           g_sd_fmt.busy ? "true" : "false", g_sd_fmt.done ? "true" : "false",
           g_sd_fmt.ok ? "true" : "false", g_sd_fmt.percent, status_esc, msg_esc);
  g_http->send(200, "application/json", body);
}

void handleSdFormatPost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected body: FORMAT");
    return;
  }
  String body = g_http->arg("plain");
  body.trim();
  if (body != "FORMAT") {
    g_http->send(400, "text/plain", "Send body FORMAT to confirm wipe");
    return;
  }
  if (g_sd_fmt.busy) {
    g_http->send(409, "text/plain", "Format already running");
    return;
  }

  Serial.println("WWW: SD format requested");
  g_sd_fmt.done = false;
  g_sd_fmt.ok = false;
  g_sd_fmt.percent = 0;
  g_sd_fmt.status[0] = '\0';
  g_sd_fmt.message[0] = '\0';
  g_sd_fmt.busy = true;

  const BaseType_t created =
      xTaskCreate(sdFormatTask, "sd_fmt", 12288, nullptr, 1, nullptr);
  if (created != pdPASS) {
    g_sd_fmt.busy = false;
    g_http->send(500, "text/plain", "Could not start format task");
    return;
  }
  g_http->send(202, "application/json", "{\"started\":true}");
}

void registerConfigRoutes() {
  // Literals must be static — WebServer keeps the pointer.
  g_http->on("/api/config/network", HTTP_GET, handleConfigGet);
  g_http->on("/api/config/network", HTTP_POST, handleConfigPost);
  g_http->on("/api/config/bluetooth", HTTP_GET, handleConfigGet);
  g_http->on("/api/config/bluetooth", HTTP_POST, handleConfigPost);
  g_http->on("/api/config/mouse", HTTP_GET, handleConfigGet);
  g_http->on("/api/config/mouse", HTTP_POST, handleConfigPost);
  g_http->on("/api/config/keyboard", HTTP_GET, handleConfigGet);
  g_http->on("/api/config/keyboard", HTTP_POST, handleConfigPost);
  g_http->on("/api/config/sd", HTTP_GET, handleConfigGet);
  g_http->on("/api/config/sd", HTTP_POST, handleConfigPost);
  g_http->on("/api/config/ui", HTTP_GET, handleConfigGet);
  g_http->on("/api/config/ui", HTTP_POST, handleConfigPost);
  g_http->on("/api/config/display", HTTP_GET, handleConfigGet);
  g_http->on("/api/config/display", HTTP_POST, handleConfigPost);
  g_http->on("/api/config/backup", HTTP_GET, handleBackupGet);
  g_http->on("/api/config/backup", HTTP_POST, handleBackupPost, handleBackupUpload);
  g_http->on("/api/device/status", HTTP_GET, handleDeviceStatusGet);
  g_http->on("/api/device/preview", HTTP_POST, handleDevicePreviewPost);
  g_http->on("/api/wifi/status", HTTP_GET, handleWifiStatusGet);
  g_http->on("/api/wifi/scan", HTTP_GET, handleWifiScanGet);
  g_http->on("/api/wifi/connect", HTTP_POST, handleWifiConnectPost);
  g_http->on("/api/wifi/profiles", HTTP_GET, handleWifiProfilesGet);
  g_http->on("/api/wifi/profiles/delete", HTTP_POST, handleWifiProfilesDeletePost);
  g_http->on("/api/wifi/profiles/update", HTTP_POST, handleWifiProfilesUpdatePost);
  g_http->on("/api/sd/icon", HTTP_GET, handleSdIconGet);
  g_http->on("/api/sd/status", HTTP_GET, handleSdStatusGet);
  g_http->on("/api/sd/expose", HTTP_POST, handleSdExposePost);
  g_http->on("/api/sd/format", HTTP_POST, handleSdFormatPost);
  g_http->on("/api/sd/format/progress", HTTP_GET, handleSdFormatProgressGet);
}

void onWsEvent(uint8_t /*num*/, WStype_t type, uint8_t* payload, size_t length) {
  if (!g_server) {
    return;
  }

  if (type == WStype_CONNECTED) {
    g_server->setClientCount(static_cast<uint8_t>(g_server->clientCount() + 1));
  } else if (type == WStype_DISCONNECTED) {
    const uint8_t n = g_server->clientCount();
    if (n > 0) {
      g_server->setClientCount(static_cast<uint8_t>(n - 1));
    }
  } else if (type == WStype_TEXT && payload && length > 0) {
    char buf[256];
    const size_t n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = '\0';
    hid::HidProtocol::processMessage(buf);
  }
}

}  // namespace

namespace remote_hid {

void RemoteHidServer::begin() {
  if (running_) {
    return;
  }

  auto& cfg = storage::ConfigStore::instance();
  cfg.loadSettings();
  cfg.loadBluetooth();
  cfg.loadMouse();
  cfg.loadKeyboard();
  cfg.loadUi();
  cfg.loadDisplay();
  display::applyFromConfig();
  led::RgbLed::instance().setBrightnessPercent(cfg.lightBrightness());

  if (!g_http || g_http_port != cfg.webPort()) {
    delete g_http;
    g_http = new WebServer(cfg.webPort());
    g_http_port = cfg.webPort();
  }
  if (!g_ws || g_ws_port != cfg.wsPort()) {
    delete g_ws;
    g_ws = new WebSocketsServer(cfg.wsPort());
    g_ws_port = cfg.wsPort();
  }

  applyWifi(true);

  seedWwwIfMissing();

  g_http->on("/", HTTP_GET, []() {
    String page;
    if (!loadPage(page)) {
      g_http->send(500, "text/plain", "Missing /www/index.html");
      return;
    }
    auto& c = storage::ConfigStore::instance();
    page.replace("__PO_REMOTE_TITLE__", branding::kRemotePageTitle);
    page.replace("__PO_WS_PORT__", String(c.wsPort()));
    g_http->send(200, "text/html", page);
  });
  g_http->on("/css/app.css", HTTP_GET, handleWwwAssetGet);
  // Catch-all for /css/* and /js/* (WebServer matches exact paths for on(); use notFound filter).
  registerConfigRoutes();
  g_http->onNotFound([]() {
    const String uri = g_http->uri();
    if (uri.startsWith("/css/") || uri.startsWith("/js/")) {
      handleWwwAssetGet();
      return;
    }
    g_http->send(404, "text/plain", "Not found");
  });
  g_http->begin();

  g_server = this;
  clients_ = 0;
  g_ws->begin();
  g_ws->onEvent(onWsEvent);

  hid::UsbHid::instance().begin();
  running_ = true;
}

void RemoteHidServer::loop() {
  if (!running_ || !g_http || !g_ws) {
    return;
  }
  g_http->handleClient();
  g_ws->loop();
}

void RemoteHidServer::end() {
  if (!running_) {
    return;
  }
  if (g_ws) {
    g_ws->close();
  }
  if (g_http) {
    g_http->stop();
  }
  WiFi.disconnect(true, false);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  g_server = nullptr;
  clients_ = 0;
  running_ = false;
}

IPAddress RemoteHidServer::apIp() const { return WiFi.softAPIP(); }

IPAddress RemoteHidServer::staIp() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP();
  }
  return IPAddress();
}

IPAddress RemoteHidServer::pageIp() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP();
  }
  return WiFi.softAPIP();
}

}  // namespace remote_hid
