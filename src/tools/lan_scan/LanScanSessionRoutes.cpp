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

void handleInfoGet() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  IPAddress from, to;
  subnetRange(from, to);
  const IPAddress ip = WiFi.localIP();
  const IPAddress mask = WiFi.subnetMask();
  const IPAddress gw = WiFi.gatewayIP();
  const IPAddress dns1 = WiFi.dnsIP(0);
  const IPAddress dns2 = WiFi.dnsIP(1);
  char bssid[18] = {};
  if (WiFi.BSSID()) {
    formatMac(WiFi.BSSID(), bssid, sizeof(bssid));
  }
  char ssid_esc[68];
  char bssid_esc[40];
  char host_esc[48];
  jsonEsc(WiFi.SSID().c_str(), ssid_esc, sizeof(ssid_esc));
  jsonEsc(bssid, bssid_esc, sizeof(bssid_esc));
  jsonEsc(WiFi.getHostname() ? WiFi.getHostname() : "", host_esc, sizeof(host_esc));

  const bool has_prof = storage::LanProfiles::instance().has(bssid);

  char body[768];
  snprintf(body, sizeof(body),
           "{\"sta\":true,\"ip\":\"%u.%u.%u.%u\",\"mask\":\"%u.%u.%u.%u\","
           "\"gateway\":\"%u.%u.%u.%u\",\"dns1\":\"%u.%u.%u.%u\",\"dns2\":\"%u.%u.%u.%u\","
           "\"ssid\":\"%s\",\"bssid\":\"%s\",\"rssi\":%d,\"hostname\":\"%s\","
           "\"from\":\"%u.%u.%u.%u\",\"to\":\"%u.%u.%u.%u\",\"profile\":%s,\"profiles\":%u}",
           ip[0], ip[1], ip[2], ip[3], mask[0], mask[1], mask[2], mask[3], gw[0], gw[1], gw[2],
           gw[3], dns1[0], dns1[1], dns1[2], dns1[3], dns2[0], dns2[1], dns2[2], dns2[3],
           ssid_esc, bssid_esc, WiFi.RSSI(), host_esc, from[0], from[1], from[2], from[3], to[0],
           to[1], to[2], to[3], has_prof ? "true" : "false",
           static_cast<unsigned>(storage::LanProfiles::instance().count()));
  g_http->send(200, "application/json", body);
}

void handleCancelPost() {
  if (g_job) {
    g_job->cancel = true;
    g_job->watch_on = false;
  }
  g_http->send(200, "application/json", "{\"ok\":true}");
}

void handleSessionPost() {
  String body = readBody();
  body.toLowerCase();
  const bool closing =
      body.indexOf("close") >= 0 || body == "0" || body == "off" || body.indexOf("false") >= 0;
  if (closing) {
    releaseLanBuffers();
    g_http->send(200, "application/json", "{\"ok\":true,\"session\":false}");
    return;
  }
  if (!ensureLanBuffers()) {
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  // Mount discover/tools/profiles on first open (SoftAP boot only had /session).
  if (!ensureLanWorkRoutes()) {
    releaseLanBuffers();
    g_http->send(500, "application/json", "{\"error\":\"routes\"}");
    return;
  }
  g_http->send(200, "application/json", "{\"ok\":true,\"session\":true}");
}

void handleWatchPost() {
  String body = readBody();
  String en = jsonField(body, "enabled");
  int mins = jsonField(body, "interval_min").toInt();
  if (mins < 1) {
    mins = 5;
  }
  if (mins > 60) {
    mins = 60;
  }
  if (!ensureLanBuffers() || !g_job) {
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  g_job->watch_on = (en == "true" || en == "1");
  g_job->watch_interval_ms = static_cast<uint32_t>(mins) * 60000u;
  g_job->watch_last_ms = millis();
  g_http->send(200, "application/json",
               String("{\"ok\":true,\"enabled\":") + (g_job->watch_on ? "true" : "false") +
                   ",\"interval_min\":" + String(mins) + "}");
}

void handleWatchGet() {
  if (!g_job) {
    g_http->send(200, "application/json", "{\"enabled\":false,\"interval_min\":5,\"busy\":false}");
    return;
  }
  char body[128];
  snprintf(body, sizeof(body), "{\"enabled\":%s,\"interval_min\":%u,\"busy\":%s}",
           g_job->watch_on ? "true" : "false",
           static_cast<unsigned>(g_job->watch_interval_ms / 60000u),
           g_job->busy ? "true" : "false");
  g_http->send(200, "application/json", body);
}

void handleStatusGet() {
  if (!g_job) {
    g_http->send(200, "application/json",
                 "{\"busy\":false,\"kind\":\"none\",\"progress\":0,\"scanned\":0,\"total\":0,"
                 "\"message\":\"\"}");
    return;
  }
  const char* kind = "none";
  switch (g_job->kind) {
    case JobKind::Discover:
      kind = "discover";
      break;
    case JobKind::Ports:
      kind = "ports";
      break;
    case JobKind::Ping:
      kind = "ping";
      break;
    case JobKind::Mdns:
      kind = "mdns";
      break;
    case JobKind::Traceroute:
      kind = "traceroute";
      break;
    default:
      break;
  }
  char body[192];
  snprintf(body, sizeof(body),
           "{\"busy\":%s,\"kind\":\"%s\",\"progress\":%d,\"scanned\":%d,\"total\":%d,\"message\":\"%"
           "s\"}",
           g_job->busy ? "true" : "false", kind, g_job->progress, g_job->scanned, g_job->total,
           g_job->message);
  g_http->send(200, "application/json", body);
}


void registerLanSessionRoutes(WebServer& http) {
  // Only the front desk at SoftAP boot — rest mounts on session open.
  http.on("/api/lan/session", HTTP_POST, handleSessionPost);
}

}  // namespace lan_scan
