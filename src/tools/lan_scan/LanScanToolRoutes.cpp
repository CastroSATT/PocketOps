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

void handlePingPost() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  String body = readBody();
  String ip_s = jsonField(body, "ip");
  int count = jsonField(body, "count").toInt();
  if (count <= 0) {
    count = 4;
  }
  IPAddress ip;
  if (!parseIp(ip_s.c_str(), ip)) {
    g_http->send(400, "application/json", "{\"error\":\"ip\"}");
    return;
  }
  auto* ta = new TaskArgs();
  ta->kind = JobKind::Ping;
  ta->a = ipToU32(ip);
  ta->count = count;
  ta->ports = nullptr;
  ta->nports = 0;
  if (!startJob(ta)) {
    g_http->send(500, "application/json", "{\"error\":\"task\"}");
    return;
  }
  g_http->send(202, "application/json", "{\"started\":true}");
}

void handlePingGet() {
  if (!g_job) {
    g_http->send(200, "application/json",
                 "{\"busy\":false,\"done\":false,\"progress\":0,\"sent\":0,\"recv\":0,"
                 "\"loss_pct\":0,\"min\":0,\"avg\":0,\"max\":0,\"message\":\"\"}");
    return;
  }
  const int loss =
      g_job->ping_sent ? (100 * (g_job->ping_sent - g_job->ping_recv)) / g_job->ping_sent : 0;
  char body[256];
  snprintf(body, sizeof(body),
           "{\"busy\":%s,\"done\":%s,\"progress\":%d,\"sent\":%d,\"recv\":%d,"
           "\"loss_pct\":%d,\"min\":%d,\"avg\":%d,\"max\":%d,\"message\":\"%s\"}",
           (g_job->busy && g_job->kind == JobKind::Ping) ? "true" : "false",
           g_job->done ? "true" : "false", g_job->progress, g_job->ping_sent, g_job->ping_recv, loss,
           g_job->ping_min, g_job->ping_avg, g_job->ping_max, g_job->message);
  g_http->send(200, "application/json", body);
}

bool buildPortList(const String& mode, const String& custom, uint16_t** out, size_t* nout) {
  *out = nullptr;
  *nout = 0;
  if (mode == "quick") {
    *nout = kQuickPortsCount;
    *out = static_cast<uint16_t*>(malloc((*nout) * sizeof(uint16_t)));
    if (!*out) {
      return false;
    }
    memcpy(*out, kQuickPorts, (*nout) * sizeof(uint16_t));
    return true;
  }
  if (mode == "common" || mode == "") {
    *nout = kCommonPortsCount;
    *out = static_cast<uint16_t*>(malloc((*nout) * sizeof(uint16_t)));
    if (!*out) {
      return false;
    }
    memcpy(*out, kCommonPorts, (*nout) * sizeof(uint16_t));
    return true;
  }
  if (mode == "full") {
    *out = nullptr;
    *nout = 0;
    return true;  // caller uses full_ports path
  }
  // custom: "80,443" or "1-1024" — parse scratch lives on LanSlab (not BSS).
  if (!g_lan_slab && !ensureLanBuffers()) {
    return false;
  }
  uint16_t* tmp = g_lan_slab->port_tmp;
  size_t n = 0;
  const char* s = custom.length() ? custom.c_str() : mode.c_str();
  const size_t slen = strlen(s);
  size_t start = 0;
  while (start < slen && n < kPortTmpCap) {
    size_t comma = start;
    while (comma < slen && s[comma] != ',') {
      ++comma;
    }
    // Trim whitespace on [start, comma).
    size_t a0 = start;
    size_t a1 = comma;
    while (a0 < a1 && (s[a0] == ' ' || s[a0] == '\t')) {
      ++a0;
    }
    while (a1 > a0 && (s[a1 - 1] == ' ' || s[a1 - 1] == '\t')) {
      --a1;
    }
    size_t dash = a0;
    bool have_dash = false;
    for (size_t i = a0; i < a1; ++i) {
      if (s[i] == '-') {
        dash = i;
        have_dash = true;
        break;
      }
    }
    if (have_dash && dash > a0) {
      int a = atoi(s + a0);
      int b = atoi(s + dash + 1);
      if (a < 1) {
        a = 1;
      }
      if (b > 65535) {
        b = 65535;
      }
      if (b < a) {
        const int t = a;
        a = b;
        b = t;
      }
      for (int p = a; p <= b && n < kPortTmpCap; ++p) {
        tmp[n++] = static_cast<uint16_t>(p);
      }
    } else if (a1 > a0) {
      const int p = atoi(s + a0);
      if (p >= 1 && p <= 65535) {
        tmp[n++] = static_cast<uint16_t>(p);
      }
    }
    start = comma + 1;
  }
  if (!n) {
    return false;
  }
  *out = static_cast<uint16_t*>(malloc(n * sizeof(uint16_t)));
  if (!*out) {
    return false;
  }
  memcpy(*out, tmp, n * sizeof(uint16_t));
  *nout = n;
  return true;
}

void handlePortsPost() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  String body = readBody();
  String ip_s = jsonField(body, "ip");
  String mode = jsonField(body, "mode");
  String ports = jsonField(body, "ports");
  IPAddress ip;
  if (!parseIp(ip_s.c_str(), ip)) {
    g_http->send(400, "application/json", "{\"error\":\"ip\"}");
    return;
  }
  const bool full = (mode == "full");
  uint16_t* list = nullptr;
  size_t n = 0;
  if (!full && !buildPortList(mode, ports, &list, &n)) {
    g_http->send(400, "application/json", "{\"error\":\"ports\"}");
    return;
  }
  if (!ensureLanBuffers() || !g_lan_slab) {
    free(list);
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  g_lan_slab->dyn_ports = list;
  g_lan_slab->dyn_n = n;
  auto* ta = new TaskArgs();
  ta->kind = JobKind::Ports;
  ta->a = ipToU32(ip);
  ta->ports = g_lan_slab->dyn_ports;
  ta->nports = g_lan_slab->dyn_n;
  ta->count = 0;
  ta->full_ports = full;
  if (!startJob(ta)) {
    free(g_lan_slab->dyn_ports);
    g_lan_slab->dyn_ports = nullptr;
    g_lan_slab->dyn_n = 0;
    g_http->send(500, "application/json", "{\"error\":\"task\"}");
    return;
  }
  g_http->send(202, "application/json", "{\"started\":true}");
}

void handlePortsGet() {
  if (!g_job) {
    g_http->send(200, "application/json",
                 "{\"busy\":false,\"done\":false,\"progress\":0,\"scanned\":0,\"total\":0,"
                 "\"ip\":\"\",\"open\":[]}");
    return;
  }
  char* arena = allocJsonArena();
  if (!arena) {
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  JsonW w;
  w.begin(arena, kJsonArenaCap);
  char ip_e[24];
  jsonEsc(g_job->target_ip, ip_e, sizeof(ip_e));
  w.put("{\"busy\":");
  w.put((g_job->busy && g_job->kind == JobKind::Ports) ? "true" : "false");
  w.put(",\"done\":");
  w.put(g_job->done ? "true" : "false");
  w.put(",\"progress\":");
  w.puti(g_job->progress);
  w.put(",\"scanned\":");
  w.puti(g_job->scanned);
  w.put(",\"total\":");
  w.puti(g_job->total);
  w.put(",\"ip\":\"");
  w.put(ip_e);
  w.put("\",\"open\":[");
  if (!g_job->open) {
    w.put("]}");
    sendJsonArenaOrError(w, arena);
    return;
  }
  lockJob();
  for (size_t i = 0; i < g_job->open_count; ++i) {
    if (i && !w.putc(',')) {
      unlockJob();
      sendJsonArenaOrError(w, arena);
      return;
    }
    const OpenPort& o = g_job->open[i];
    const char* svc = serviceName(o.port);
    char svc_e[24];
    jsonEsc(svc, svc_e, sizeof(svc_e));
    // Keep API shape; banner/title no longer resident in RAM.
    if (!w.put("{\"port\":") || !w.puti(o.port) || !w.put(",\"service\":\"") || !w.put(svc_e) ||
        !w.put("\",\"banner\":\"\",\"http_status\":") || !w.puti(o.http_status) ||
        !w.put(",\"http_title\":\"\"}")) {
      unlockJob();
      sendJsonArenaOrError(w, arena);
      return;
    }
  }
  unlockJob();
  w.put("]}");
  sendJsonArenaOrError(w, arena);
}

void handleWolPost() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  String body = readBody();
  String mac_s = jsonField(body, "mac");
  uint8_t mac[6] = {};
  int vals[6] = {};
  if (sscanf(mac_s.c_str(), "%x:%x:%x:%x:%x:%x", &vals[0], &vals[1], &vals[2], &vals[3], &vals[4],
             &vals[5]) != 6 &&
      sscanf(mac_s.c_str(), "%x-%x-%x-%x-%x-%x", &vals[0], &vals[1], &vals[2], &vals[3], &vals[4],
             &vals[5]) != 6) {
    g_http->send(400, "application/json", "{\"error\":\"mac\"}");
    return;
  }
  for (int i = 0; i < 6; ++i) {
    mac[i] = static_cast<uint8_t>(vals[i]);
  }
  uint8_t pkt[102];
  memset(pkt, 0xFF, 6);
  for (int i = 0; i < 16; ++i) {
    memcpy(pkt + 6 + i * 6, mac, 6);
  }
  WiFiUDP u;
  IPAddress bcast = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  for (int i = 0; i < 4; ++i) {
    bcast[i] = uint8_t(bcast[i] | ~mask[i]);
  }
  u.beginPacket(bcast, 9);
  u.write(pkt, sizeof(pkt));
  u.endPacket();
  u.beginPacket(IPAddress(255, 255, 255, 255), 9);
  u.write(pkt, sizeof(pkt));
  u.endPacket();
  u.stop();
  g_http->send(200, "application/json", "{\"ok\":true}");
}

void handleDnsPost() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  String body = readBody();
  String q = jsonField(body, "q");
  String type = jsonField(body, "type");
  if (type == "reverse") {
    IPAddress ip;
    if (!parseIp(q.c_str(), ip)) {
      g_http->send(400, "application/json", "{\"error\":\"ip\"}");
      return;
    }
    // Best-effort: no reverse DNS API on all builds — return empty hostname.
    g_http->send(200, "application/json", "{\"q\":\"" + q + "\",\"result\":\"\"}");
    return;
  }
  IPAddress ip;
  if (!WiFi.hostByName(q.c_str(), ip)) {
    g_http->send(200, "application/json", "{\"q\":\"" + q + "\",\"result\":\"\"}");
    return;
  }
  char body_out[128];
  snprintf(body_out, sizeof(body_out), "{\"q\":\"%s\",\"result\":\"%u.%u.%u.%u\"}", q.c_str(), ip[0],
           ip[1], ip[2], ip[3]);
  g_http->send(200, "application/json", body_out);
}

void handleMdnsPost() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  auto* ta = new TaskArgs();
  ta->kind = JobKind::Mdns;
  ta->a = ta->b = 0;
  ta->ports = nullptr;
  ta->nports = 0;
  ta->count = 0;
  if (!startJob(ta)) {
    g_http->send(500, "application/json", "{\"error\":\"task\"}");
    return;
  }
  g_http->send(202, "application/json", "{\"started\":true}");
}

void handleMdnsGet() {
  if (!g_job) {
    g_http->send(200, "application/json", "{\"busy\":false,\"done\":false,\"services\":[]}");
    return;
  }
  String json = "{\"busy\":";
  json += (g_job->busy && g_job->kind == JobKind::Mdns) ? "true" : "false";
  json += ",\"done\":";
  json += g_job->done ? "true" : "false";
  json += ",\"services\":[";
  lockJob();
  for (size_t i = 0; i < g_job->mdns_count; ++i) {
    if (i) {
      json += ',';
    }
    char n_e[80], ip_e[24], t_e[60];
    jsonEsc(g_job->mdns[i].name, n_e, sizeof(n_e));
    jsonEsc(g_job->mdns[i].ip, ip_e, sizeof(ip_e));
    jsonEsc(g_job->mdns[i].type, t_e, sizeof(t_e));
    json += "{\"name\":\"";
    json += n_e;
    json += "\",\"ip\":\"";
    json += ip_e;
    json += "\",\"port\":";
    json += String(g_job->mdns[i].port);
    json += ",\"type\":\"";
    json += t_e;
    json += "\"}";
  }
  unlockJob();
  json += "]}";
  g_http->send(200, "application/json", json);
}

void handleTraceroutePost() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  String body = readBody();
  String ip_s = jsonField(body, "ip");
  int hops = jsonField(body, "max_hops").toInt();
  if (hops <= 0) {
    hops = 8;
  }
  IPAddress ip;
  if (!parseIp(ip_s.c_str(), ip)) {
    g_http->send(400, "application/json", "{\"error\":\"ip\"}");
    return;
  }
  auto* ta = new TaskArgs();
  ta->kind = JobKind::Traceroute;
  ta->a = ipToU32(ip);
  ta->count = hops;
  ta->ports = nullptr;
  ta->nports = 0;
  if (!startJob(ta)) {
    g_http->send(500, "application/json", "{\"error\":\"task\"}");
    return;
  }
  g_http->send(202, "application/json", "{\"started\":true}");
}

void handleTracerouteGet() {
  if (!g_job) {
    g_http->send(200, "application/json", "{\"busy\":false,\"done\":false,\"hops\":[]}");
    return;
  }
  String json = "{\"busy\":";
  json += (g_job->busy && g_job->kind == JobKind::Traceroute) ? "true" : "false";
  json += ",\"done\":";
  json += g_job->done ? "true" : "false";
  json += ",\"hops\":[";
  for (size_t i = 0; i < g_job->hop_count; ++i) {
    if (i) {
      json += ',';
    }
    json += "{\"ip\":\"";
    json += g_job->hops[i].ip;
    json += "\",\"rtt_ms\":";
    json += String(g_job->hops[i].rtt_ms);
    json += '}';
  }
  json += "]}";
  g_http->send(200, "application/json", json);
}


void registerLanToolRoutes(WebServer& http) {
  http.on("/api/lan/ping", HTTP_POST, handlePingPost);
  http.on("/api/lan/ping", HTTP_GET, handlePingGet);
  http.on("/api/lan/ports", HTTP_POST, handlePortsPost);
  http.on("/api/lan/ports", HTTP_GET, handlePortsGet);
  http.on("/api/lan/wol", HTTP_POST, handleWolPost);
  http.on("/api/lan/dns", HTTP_POST, handleDnsPost);
  http.on("/api/lan/mdns", HTTP_POST, handleMdnsPost);
  http.on("/api/lan/mdns", HTTP_GET, handleMdnsGet);
  http.on("/api/lan/traceroute", HTTP_POST, handleTraceroutePost);
  http.on("/api/lan/traceroute", HTTP_GET, handleTracerouteGet);
}

}  // namespace lan_scan
