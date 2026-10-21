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

void handleDiscoverGet() {
  if (!g_job) {
    g_http->send(200, "application/json",
                 "{\"busy\":false,\"done\":false,\"progress\":0,\"scanned\":0,\"total\":0,"
                 "\"message\":\"\",\"hosts\":[]}");
    return;
  }
  char* arena = allocJsonArena();
  if (!arena) {
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  JsonW w;
  w.begin(arena, kJsonArenaCap);
  char msg_e[120];
  jsonEsc(g_job->message, msg_e, sizeof(msg_e));
  w.put("{\"busy\":");
  w.put(g_job->busy && g_job->kind == JobKind::Discover ? "true" : "false");
  w.put(",\"done\":");
  w.put(g_job->done ? "true" : "false");
  w.put(",\"progress\":");
  w.puti(g_job->progress);
  w.put(",\"scanned\":");
  w.puti(g_job->scanned);
  w.put(",\"total\":");
  w.puti(g_job->total);
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
  if (!g_job->hosts) {
    return;
  }
  lockJob();
  for (size_t i = 0; i < g_job->host_count; ++i) {
    const Host& h = g_job->hosts[i];
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
  if (g_job->host_count == 0) {
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



void registerLanDiscoverRoutes(WebServer& http) {
  http.on("/api/lan/discover", HTTP_POST, handleDiscoverPost);
  http.on("/api/lan/discover", HTTP_GET, handleDiscoverGet);
  http.on("/api/lan/hosts", HTTP_GET, handleHostsGet);
  http.on("/api/lan/export.csv", HTTP_GET, handleExportCsvGet);
  http.on("/api/lan/export/sd", HTTP_POST, handleExportSdPost);
  http.on("/api/lan/profiles/autoload", HTTP_GET, handleAutoloadGet);
}

}  // namespace lan_scan
