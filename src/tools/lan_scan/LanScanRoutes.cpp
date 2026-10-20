#include "tools/lan_scan/LanScanRoutes.h"

#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "storage/DeviceLog.h"
#include "storage/LanProfiles.h"
#include "storage/TfExpose.h"
#include "tools/lan_scan/LanScanJobs.h"
#include "tools/lan_scan/LanScanNet.h"
#include "tools/lan_scan/LanScanState.h"
#include "tools/lan_scan/OuiTable.h"
#include "util/JsonEsc.h"

namespace lan_scan {
namespace {

using util::jsonEscape;
inline void jsonEsc(const char* in, char* out, size_t out_len) {
  jsonEscape(in, out, out_len);
}

String readBody() {
  if (!g_http->hasArg("plain")) {
    return String();
  }
  return g_http->arg("plain");
}

String jsonField(const String& body, const char* key) {
  const String pat = String("\"") + key + "\":";
  int i = body.indexOf(pat);
  if (i < 0) {
    return String();
  }
  i += pat.length();
  while (i < static_cast<int>(body.length()) && (body[i] == ' ')) {
    ++i;
  }
  if (i >= static_cast<int>(body.length())) {
    return String();
  }
  if (body[i] == '"') {
    ++i;
    const int j = body.indexOf('"', i);
    if (j < 0) {
      return String();
    }
    return body.substring(i, j);
  }
  int j = i;
  while (j < static_cast<int>(body.length()) &&
         (isdigit(body[j]) || body[j] == '.' || body[j] == '-' || isalpha(body[j]) ||
          body[j] == '_')) {
    ++j;
  }
  return body.substring(i, j);
}

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
  g_job.cancel = true;
  g_job.watch_on = false;
  g_http->send(200, "application/json", "{\"ok\":true}");
}

void handleDiscoverPost() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  String body = readBody();
  IPAddress from, to;
  subnetRange(from, to);
  const String fs = jsonField(body, "from");
  const String ts = jsonField(body, "to");
  if (fs.length()) {
    parseIp(fs.c_str(), from);
  }
  if (ts.length()) {
    parseIp(ts.c_str(), to);
  }
  auto* ta = new TaskArgs();
  ta->kind = JobKind::Discover;
  ta->a = ipToU32(from);
  ta->b = ipToU32(to);
  ta->ports = nullptr;
  ta->nports = 0;
  ta->count = 0;
  if (!startJob(ta)) {
    g_http->send(500, "application/json", "{\"error\":\"task\"}");
    return;
  }
  g_http->send(202, "application/json", "{\"started\":true}");
}

// Heap arena per response — avoids String += fragmentation without permanent BSS.
constexpr size_t kJsonArenaCap = 28672;

struct JsonW {
  char* p = nullptr;
  size_t n = 0;
  size_t cap = 0;
  bool ok = true;

  void begin(char* buf, size_t c) {
    p = buf;
    n = 0;
    cap = c;
    ok = true;
    if (p && cap) {
      p[0] = '\0';
    }
  }

  bool put(const char* s) {
    if (!ok || !p || !s) {
      return ok;
    }
    const size_t L = strlen(s);
    if (n + L + 1 > cap) {
      ok = false;
      return false;
    }
    memcpy(p + n, s, L);
    n += L;
    p[n] = '\0';
    return true;
  }

  bool putc(char c) {
    if (!ok || !p || n + 2 > cap) {
      ok = false;
      return false;
    }
    p[n++] = c;
    p[n] = '\0';
    return true;
  }

  bool puti(long v) {
    char t[16];
    snprintf(t, sizeof(t), "%ld", v);
    return put(t);
  }
};

bool appendHostsJson(JsonW& w) {
  if (!w.put("\"hosts\":[")) {
    return false;
  }
  lockJob();
  for (size_t i = 0; i < g_job.host_count; ++i) {
    if (i && !w.putc(',')) {
      unlockJob();
      return false;
    }
    const Host& h = g_job.hosts[i];
    char ip_e[24], mac_e[40], hn_e[80], ven_e[48], notes_e[128], ports_e[160];
    jsonEsc(h.ip, ip_e, sizeof(ip_e));
    jsonEsc(h.mac, mac_e, sizeof(mac_e));
    jsonEsc(h.hostname, hn_e, sizeof(hn_e));
    jsonEsc(h.vendor, ven_e, sizeof(ven_e));
    jsonEsc(h.notes, notes_e, sizeof(notes_e));
    jsonEsc(h.open_ports, ports_e, sizeof(ports_e));
    if (!w.put("{\"ip\":\"") || !w.put(ip_e) || !w.put("\",\"mac\":\"") || !w.put(mac_e) ||
        !w.put("\",\"hostname\":\"") || !w.put(hn_e) || !w.put("\",\"vendor\":\"") ||
        !w.put(ven_e) || !w.put("\",\"notes\":\"") || !w.put(notes_e) ||
        !w.put("\",\"open_ports\":\"") || !w.put(ports_e) || !w.put("\",\"rtt_ms\":") ||
        !w.puti(h.rtt_ms) || !w.put(",\"stale\":") || !w.put(h.stale ? "true" : "false") ||
        !w.put(",\"new\":") || !w.put(h.is_new ? "true" : "false") || !w.put(",\"missing\":") ||
        !w.put(h.missing ? "true" : "false") || !w.putc('}')) {
      unlockJob();
      return false;
    }
  }
  unlockJob();
  return w.putc(']');
}

char* allocJsonArena() {
  return static_cast<char*>(malloc(kJsonArenaCap));
}

void sendJsonArenaOrError(JsonW& w, char* arena) {
  if (!arena) {
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  if (!w.ok) {
    free(arena);
    g_http->send(500, "application/json", "{\"error\":\"json_overflow\"}");
    return;
  }
  g_http->send(200, "application/json", arena);
  free(arena);
}

void handleDiscoverGet() {
  char* arena = allocJsonArena();
  if (!arena) {
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  JsonW w;
  w.begin(arena, kJsonArenaCap);
  char msg_e[120];
  jsonEsc(g_job.message, msg_e, sizeof(msg_e));
  w.put("{\"busy\":");
  w.put(g_job.busy && g_job.kind == JobKind::Discover ? "true" : "false");
  w.put(",\"done\":");
  w.put(g_job.done ? "true" : "false");
  w.put(",\"progress\":");
  w.puti(g_job.progress);
  w.put(",\"scanned\":");
  w.puti(g_job.scanned);
  w.put(",\"total\":");
  w.puti(g_job.total);
  w.put(",\"message\":\"");
  w.put(msg_e);
  w.put("\",");
  appendHostsJson(w);
  w.putc('}');
  sendJsonArenaOrError(w, arena);
}

void handleHostsGet() {
  char* arena = allocJsonArena();
  if (!arena) {
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  JsonW w;
  w.begin(arena, kJsonArenaCap);
  w.putc('{');
  appendHostsJson(w);
  w.putc('}');
  sendJsonArenaOrError(w, arena);
}

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
  const int loss =
      g_job.ping_sent ? (100 * (g_job.ping_sent - g_job.ping_recv)) / g_job.ping_sent : 0;
  char body[256];
  snprintf(body, sizeof(body),
           "{\"busy\":%s,\"done\":%s,\"progress\":%d,\"sent\":%d,\"recv\":%d,"
           "\"loss_pct\":%d,\"min\":%d,\"avg\":%d,\"max\":%d,\"message\":\"%s\"}",
           (g_job.busy && g_job.kind == JobKind::Ping) ? "true" : "false",
           g_job.done ? "true" : "false", g_job.progress, g_job.ping_sent, g_job.ping_recv, loss,
           g_job.ping_min, g_job.ping_avg, g_job.ping_max, g_job.message);
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
  // custom: "80,443" or "1-1024" — static temp, not on the HTTP stack.
  static uint16_t s_port_tmp[1024];
  size_t n = 0;
  const char* s = custom.length() ? custom.c_str() : mode.c_str();
  const size_t slen = strlen(s);
  size_t start = 0;
  while (start < slen && n < 1024) {
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
      for (int p = a; p <= b && n < 1024; ++p) {
        s_port_tmp[n++] = static_cast<uint16_t>(p);
      }
    } else if (a1 > a0) {
      const int p = atoi(s + a0);
      if (p >= 1 && p <= 65535) {
        s_port_tmp[n++] = static_cast<uint16_t>(p);
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
  memcpy(*out, s_port_tmp, n * sizeof(uint16_t));
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
  g_dyn_ports = list;
  g_dyn_n = n;
  auto* ta = new TaskArgs();
  ta->kind = JobKind::Ports;
  ta->a = ipToU32(ip);
  ta->ports = g_dyn_ports;
  ta->nports = g_dyn_n;
  ta->count = 0;
  ta->full_ports = full;
  if (!startJob(ta)) {
    free(g_dyn_ports);
    g_dyn_ports = nullptr;
    g_http->send(500, "application/json", "{\"error\":\"task\"}");
    return;
  }
  g_http->send(202, "application/json", "{\"started\":true}");
}

void handlePortsGet() {
  char* arena = allocJsonArena();
  if (!arena) {
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  JsonW w;
  w.begin(arena, kJsonArenaCap);
  char ip_e[24];
  jsonEsc(g_job.target_ip, ip_e, sizeof(ip_e));
  w.put("{\"busy\":");
  w.put((g_job.busy && g_job.kind == JobKind::Ports) ? "true" : "false");
  w.put(",\"done\":");
  w.put(g_job.done ? "true" : "false");
  w.put(",\"progress\":");
  w.puti(g_job.progress);
  w.put(",\"scanned\":");
  w.puti(g_job.scanned);
  w.put(",\"total\":");
  w.puti(g_job.total);
  w.put(",\"ip\":\"");
  w.put(ip_e);
  w.put("\",\"open\":[");
  lockJob();
  for (size_t i = 0; i < g_job.open_count; ++i) {
    if (i && !w.putc(',')) {
      unlockJob();
      sendJsonArenaOrError(w, arena);
      return;
    }
    const OpenPort& o = g_job.open[i];
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
  String json = "{\"busy\":";
  json += (g_job.busy && g_job.kind == JobKind::Mdns) ? "true" : "false";
  json += ",\"done\":";
  json += g_job.done ? "true" : "false";
  json += ",\"services\":[";
  lockJob();
  for (size_t i = 0; i < g_job.mdns_count; ++i) {
    if (i) {
      json += ',';
    }
    char n_e[80], ip_e[24], t_e[60];
    jsonEsc(g_job.mdns[i].name, n_e, sizeof(n_e));
    jsonEsc(g_job.mdns[i].ip, ip_e, sizeof(ip_e));
    jsonEsc(g_job.mdns[i].type, t_e, sizeof(t_e));
    json += "{\"name\":\"";
    json += n_e;
    json += "\",\"ip\":\"";
    json += ip_e;
    json += "\",\"port\":";
    json += String(g_job.mdns[i].port);
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
  String json = "{\"busy\":";
  json += (g_job.busy && g_job.kind == JobKind::Traceroute) ? "true" : "false";
  json += ",\"done\":";
  json += g_job.done ? "true" : "false";
  json += ",\"hops\":[";
  for (size_t i = 0; i < g_job.hop_count; ++i) {
    if (i) {
      json += ',';
    }
    json += "{\"ip\":\"";
    json += g_job.hops[i].ip;
    json += "\",\"rtt_ms\":";
    json += String(g_job.hops[i].rtt_ms);
    json += '}';
  }
  json += "]}";
  g_http->send(200, "application/json", json);
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
  g_job.watch_on = (en == "true" || en == "1");
  g_job.watch_interval_ms = static_cast<uint32_t>(mins) * 60000u;
  g_job.watch_last_ms = millis();
  g_http->send(200, "application/json",
               String("{\"ok\":true,\"enabled\":") + (g_job.watch_on ? "true" : "false") +
                   ",\"interval_min\":" + String(mins) + "}");
}

void handleWatchGet() {
  char body[128];
  snprintf(body, sizeof(body), "{\"enabled\":%s,\"interval_min\":%u,\"busy\":%s}",
           g_job.watch_on ? "true" : "false",
           static_cast<unsigned>(g_job.watch_interval_ms / 60000u),
           g_job.busy ? "true" : "false");
  g_http->send(200, "application/json", body);
}

void handleStatusGet() {
  const char* kind = "none";
  switch (g_job.kind) {
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
           g_job.busy ? "true" : "false", kind, g_job.progress, g_job.scanned, g_job.total,
           g_job.message);
  g_http->send(200, "application/json", body);
}

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
  g_job.host_count = 0;
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
  for (size_t i = 0; i < g_job.host_count; ++i) {
    if (strcasecmp(g_job.hosts[i].mac, mac.c_str()) == 0) {
      strncpy(g_job.hosts[i].notes, note.c_str(), sizeof(g_job.hosts[i].notes) - 1);
      break;
    }
  }
  unlockJob();
  storage::LanProfiles::instance().setHostNote(bssid, mac.c_str(), note.c_str());
  g_http->send(200, "application/json", "{\"ok\":true}");
}

void sanitizeFilenamePart(const char* in, char* out, size_t out_len) {
  size_t o = 0;
  for (size_t i = 0; in && in[i] && o + 1 < out_len; ++i) {
    char c = in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_') {
      out[o++] = c;
    } else if (c == ' ' || c == '.' || c == ':') {
      out[o++] = '_';
    }
  }
  out[o] = '\0';
  if (!o) {
    strncpy(out, "unknown", out_len - 1);
  }
}

void buildCsv(String& csv) {
  csv = "ip,mac,hostname,vendor,rtt_ms,open_ports,http_title,notes\n";
  lockJob();
  for (size_t i = 0; i < g_job.host_count; ++i) {
    const Host& h = g_job.hosts[i];
    csv += h.ip;
    csv += ',';
    csv += h.mac;
    csv += ",\"";
    csv += h.hostname;
    csv += "\",\"";
    csv += h.vendor;
    csv += "\",";
    csv += String(h.rtt_ms);
    csv += ",\"";
    csv += h.open_ports;
    csv += "\",\"\",\"";  // http_title column kept empty (not stored in RAM)
    csv += h.notes;
    csv += "\"\n";
  }
  unlockJob();
}

void handleExportCsvGet() {
  String csv;
  buildCsv(csv);
  char wifi[40], mac[40], date[32];
  sanitizeFilenamePart(WiFi.SSID().c_str(), wifi, sizeof(wifi));
  char bssid[18] = {};
  if (WiFi.BSSID()) {
    formatMac(WiFi.BSSID(), bssid, sizeof(bssid));
  }
  sanitizeFilenamePart(bssid, mac, sizeof(mac));
  time_t now = time(nullptr);
  struct tm tmv {};
  if (now > 100000) {
    localtime_r(&now, &tmv);
    snprintf(date, sizeof(date), "%04d-%02d-%02d_%02d%02d", tmv.tm_year + 1900, tmv.tm_mon + 1,
             tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
  } else {
    snprintf(date, sizeof(date), "%lu", static_cast<unsigned long>(millis() / 1000));
  }
  char fname[128];
  snprintf(fname, sizeof(fname), "%s_%s_%s.csv", wifi, mac, date);
  g_http->sendHeader("Content-Disposition", String("attachment; filename=\"") + fname + "\"");
  g_http->send(200, "text/csv", csv);
}

void handleExportSdPost() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  storage::TfCardInfo info;
  if (!storage::TfExpose::instance().checkCard(info) || !info.present) {
    g_http->send(503, "application/json", "{\"error\":\"no_sd\"}");
    return;
  }
  String csv;
  buildCsv(csv);
  char wifi[40], mac[40], date[32];
  sanitizeFilenamePart(WiFi.SSID().c_str(), wifi, sizeof(wifi));
  char bssid[18] = {};
  if (WiFi.BSSID()) {
    formatMac(WiFi.BSSID(), bssid, sizeof(bssid));
  }
  sanitizeFilenamePart(bssid, mac, sizeof(mac));
  time_t now = time(nullptr);
  struct tm tmv {};
  if (now > 100000) {
    localtime_r(&now, &tmv);
    snprintf(date, sizeof(date), "%04d-%02d-%02d_%02d%02d", tmv.tm_year + 1900, tmv.tm_mon + 1,
             tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
  } else {
    snprintf(date, sizeof(date), "%lu", static_cast<unsigned long>(millis() / 1000));
  }
  char path[160];
  snprintf(path, sizeof(path), "/Logs/Lan/%s_%s_%s.csv", wifi, mac, date);
  if (!storage::TfExpose::instance().writeTextFile(path, csv.c_str())) {
    const char* err = storage::TfExpose::instance().lastError();
    char body[160];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", err && err[0] ? err : "write");
    g_http->send(500, "application/json", body);
    return;
  }
  char path_e[200];
  jsonEsc(path, path_e, sizeof(path_e));
  char body[240];
  snprintf(body, sizeof(body), "{\"ok\":true,\"path\":\"%s\"}", path_e);
  g_http->send(200, "application/json", body);
}

void handleAutoloadGet() {
  if (!staOk()) {
    sendStaRequired();
    return;
  }
  if (g_job.host_count == 0) {
    loadProfileIntoHosts();
  }
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


}  // namespace

void registerLanHttpRoutes(WebServer& http) {
  http.on("/api/lan/info", HTTP_GET, handleInfoGet);
  http.on("/api/lan/status", HTTP_GET, handleStatusGet);
  http.on("/api/lan/cancel", HTTP_POST, handleCancelPost);
  http.on("/api/lan/discover", HTTP_POST, handleDiscoverPost);
  http.on("/api/lan/discover", HTTP_GET, handleDiscoverGet);
  http.on("/api/lan/hosts", HTTP_GET, handleHostsGet);
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
  http.on("/api/lan/watch", HTTP_POST, handleWatchPost);
  http.on("/api/lan/watch", HTTP_GET, handleWatchGet);
  http.on("/api/lan/profiles", HTTP_GET, handleProfilesGet);
  http.on("/api/lan/profiles/save", HTTP_POST, handleProfilesSavePost);
  http.on("/api/lan/profiles/clear", HTTP_POST, handleProfilesClearPost);
  http.on("/api/lan/profiles/load", HTTP_POST, handleProfilesLoadPost);
  http.on("/api/lan/profiles/note", HTTP_POST, handleProfilesNotePost);
  http.on("/api/lan/profiles/autoload", HTTP_GET, handleAutoloadGet);
  http.on("/api/lan/export.csv", HTTP_GET, handleExportCsvGet);
  http.on("/api/lan/export/sd", HTTP_POST, handleExportSdPost);
}

}  // namespace lan_scan
