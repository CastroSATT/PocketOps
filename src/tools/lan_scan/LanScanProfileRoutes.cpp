#include "tools/lan_scan/LanScanHttp.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "http/HttpCtx.h"
#include "storage/DeviceLog.h"
#include "storage/LanProfiles.h"
#include "storage/TfExpose.h"
#include "tools/lan_scan/LanScanJobs.h"
#include "tools/lan_scan/LanScanNet.h"
#include "tools/lan_scan/LanScanState.h"
#include "tools/lan_scan/OuiTable.h"
#include "util/JsonEsc.h"
#include "util/JsonWriter.h"

namespace lan_scan {

using http::g_http;
using util::JsonW;
using util::jsonEscape;

void handleProfilesGet() {
  storage::LanProfiles::instance().loadIndex();
  String json = "{\"count\":";
  json += String(storage::LanProfiles::instance().count());
  json += ",\"max\":100,\"profiles\":[";
  for (size_t i = 0; i < storage::LanProfiles::instance().count(); ++i) {
    if (i) {
      json += ',';
    }
    char bs[40], ss[68];
    jsonEsc(storage::LanProfiles::instance().bssidAt(i), bs, sizeof(bs));
    jsonEsc(storage::LanProfiles::instance().ssidAt(i), ss, sizeof(ss));
    json += "{\"bssid\":\"";
    json += bs;
    json += "\",\"ssid\":\"";
    json += ss;
    json += "\",\"last_used\":";
    json += String(storage::LanProfiles::instance().lastUsedAt(i));
    json += '}';
  }
  json += "]}";
  g_http->send(200, "application/json", json);
}

void handleProfilesSavePost() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  autoSaveProfile();
  g_http->send(200, "application/json", "{\"ok\":true}");
}

void handleProfilesClearPost() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  char bssid[18] = {};
  if (WiFi.BSSID()) {
    formatMac(WiFi.BSSID(), bssid, sizeof(bssid));
  }
  storage::LanProfiles::instance().clearProfile(bssid);
  lockJob();
  g_job->host_count = 0;
  unlockJob();
  g_http->send(200, "application/json", "{\"ok\":true}");
}

void handleProfilesLoadPost() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  loadProfileIntoHosts();
  char* arena = allocJsonArena();
  if (!arena) {
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  JsonW w;
  w.begin(arena, kJsonArenaCap);
  w.put("{\"ok\":true,");
  appendHostsJson(w);
  w.putc('}');
  sendJsonArenaOrError(w, arena);
}

void handleProfilesNotePost() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  String body = readBody();
  String mac = jsonField(body, "mac");
  String note = jsonField(body, "note");
  char bssid[18] = {};
  if (WiFi.BSSID()) {
    formatMac(WiFi.BSSID(), bssid, sizeof(bssid));
  }
  // Update live host
  lockJob();
  for (size_t i = 0; i < g_job->host_count; ++i) {
    if (strcasecmp(g_job->hosts[i].mac, mac.c_str()) == 0) {
      strncpy(g_job->hosts[i].notes, note.c_str(), sizeof(g_job->hosts[i].notes) - 1);
      break;
    }
  }
  unlockJob();
  storage::LanProfiles::instance().setHostNote(bssid, mac.c_str(), note.c_str());
  g_http->send(200, "application/json", "{\"ok\":true}");
}


void registerLanProfileRoutes(WebServer& http) {
  http.on("/api/lan/profiles", HTTP_GET, handleProfilesGet);
  http.on("/api/lan/profiles/save", HTTP_POST, handleProfilesSavePost);
  http.on("/api/lan/profiles/clear", HTTP_POST, handleProfilesClearPost);
  http.on("/api/lan/profiles/load", HTTP_POST, handleProfilesLoadPost);
  http.on("/api/lan/profiles/note", HTTP_POST, handleProfilesNotePost);
}

}  // namespace lan_scan
