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
#include "util/JsonArenaPool.h"
#include "util/JsonEsc.h"
#include "util/JsonWriter.h"

namespace lan_scan {

using http::g_http;
using util::JsonW;
using util::jsonEscape;

void jsonEsc(const char* in, char* out, size_t out_len) {
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

bool appendHostsJson(JsonW& w) {
  if (!w.put("\"hosts\":[")) {
    return false;
  }
  if (!g_job || !g_job->hosts) {
    return w.putc(']');
  }
  lockJob();
  for (size_t i = 0; i < g_job->host_count; ++i) {
    if (i && !w.putc(',')) {
      unlockJob();
      return false;
    }
    const Host& h = g_job->hosts[i];
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
  return util::acquireJsonArena(kJsonArenaCap);
}

void sendJsonArenaOrError(JsonW& w, char* arena) {
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


}  // namespace lan_scan
