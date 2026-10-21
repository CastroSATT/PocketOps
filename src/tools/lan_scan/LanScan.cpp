#include "tools/lan_scan/LanScan.h"

#include <WiFi.h>

#include "http/HttpCtx.h"
#include "tools/lan_scan/LanScanJobs.h"
#include "tools/lan_scan/LanScanNet.h"
#include "tools/lan_scan/LanScanRoutes.h"
#include "tools/lan_scan/LanScanState.h"

namespace lan_scan {

void registerRoutes(WebServer& http) {
  http::setServer(&http);
  // Only /api/lan/session at boot; LanSlab + work routes on first session open.
  registerLanHttpRoutes(http);
}

bool acquireSession() {
  return ensureLanBuffers();
}

void releaseSession() {
  releaseLanBuffers();
}

void loop() {
  if (!lanBuffersReady() || !g_job || !g_job->watch_on || g_job->busy || !staOk()) {
    return;
  }
  if (millis() - g_job->watch_last_ms < g_job->watch_interval_ms) {
    return;
  }
  g_job->watch_last_ms = millis();
  IPAddress from, to;
  subnetRange(from, to);
  auto* ta = new TaskArgs();
  ta->kind = JobKind::Discover;
  ta->a = ipToU32(from);
  ta->b = ipToU32(to);
  ta->ports = nullptr;
  ta->nports = 0;
  ta->count = 0;
  startJob(ta);
}

}  // namespace lan_scan
