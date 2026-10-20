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
#include "util/JsonEsc.h"

namespace remote_hid {
namespace {

using util::jsonEscape;

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
  PoSer().printf("WIFI: SoftAP %s ch=%d ip=%s\n", cfg.wifiApSsid(), cfg.wifiApChannel(),
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
    PoSer().printf("WIFI: try %s (rssi=%d)\n", cands[i].ssid, cands[i].rssi);
    WiFi.disconnect(false, false);
    delay(30);
    WiFi.begin(cands[i].ssid, cands[i].pass);
    if (waitForSta(per_net_ms)) {
      PoSer().printf("WIFI: STA ok %s ip=%s rssi=%d\n", cands[i].ssid,
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      return true;
    }
    PoSer().printf("WIFI: fail %s\n", cands[i].ssid);
  }
  return false;
}

void applyWifi_impl(bool wait_for_sta) {
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
        PoSer().println("WIFI: STA failed — SoftAP fallback");
        WiFi.mode(WIFI_AP_STA);
        startSoftAp();
        want_ap = true;
      }
    }
  } else if (want_sta && !wait_for_sta && cfg.wifiStaSsid()[0]) {
    WiFi.begin(cfg.wifiStaSsid(), cfg.wifiStaPass());
  }

  if (want_sta && wait_for_sta && !joined) {
    PoSer().println("WIFI: no known network joined (SoftAP available)");
    PO_LOGW("wifi", "STA join failed (mode=%s)", mode);
  } else {
    PO_LOGI("wifi", "applied mode=%s ap=%d sta=%d", mode, want_ap ? 1 : 0, joined ? 1 : 0);
  }
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
  sendWifiStatus();
}


}  // namespace

void applyWifi(bool wait_for_sta) {
  applyWifi_impl(wait_for_sta);
}

void reloadConfigId(const String& id) {
  reloadConfigId_impl(id);
}

void registerWifiRoutes(WebServer& http) {
  http.on("/api/wifi/status", HTTP_GET, handleWifiStatusGet);
  http.on("/api/wifi/scan", HTTP_GET, handleWifiScanGet);
  http.on("/api/wifi/connect", HTTP_POST, handleWifiConnectPost);
  http.on("/api/wifi/profiles", HTTP_GET, handleWifiProfilesGet);
  http.on("/api/wifi/profiles/delete", HTTP_POST, handleWifiProfilesDeletePost);
  http.on("/api/wifi/profiles/update", HTTP_POST, handleWifiProfilesUpdatePost);
}

}  // namespace remote_hid
