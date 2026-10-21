#include "tools/lan_scan/LanScanState.h"

#include <WiFi.h>
#include <cstdlib>
#include <cstring>
#include <new>

#include "http/HttpCtx.h"

namespace lan_scan {

using http::g_http;

JobState* g_job = nullptr;
LanSlab* g_lan_slab = nullptr;

const uint16_t kQuickPorts[] = {22, 80, 443, 8080, 8443, 21, 23, 25, 53, 110, 139, 445,
                                3389, 5900, 554, 9100, 8000, 8888, 5000, 7001};
const uint16_t kCommonPorts[] = {
    21,   22,   23,   25,   53,   80,   81,   88,   110,  111,  135,  139,  143,  389,  443,
    445,  465,  587,  631,  993,  995,  1433, 1521, 1723, 1883, 2049, 2375, 3000, 3306, 3389,
    5432, 5500, 554,  5672, 5900, 5985, 6379, 6443, 7001, 8000, 8008, 8080, 8081, 8443, 8888,
    9000, 9090, 9100, 9200, 9418, 11211, 27017};

const size_t kQuickPortsCount = sizeof(kQuickPorts) / sizeof(kQuickPorts[0]);
const size_t kCommonPortsCount = sizeof(kCommonPorts) / sizeof(kCommonPorts[0]);

void lockJob() {
  if (g_job && g_job->lock) {
    xSemaphoreTake(g_job->lock, portMAX_DELAY);
  }
}
void unlockJob() {
  if (g_job && g_job->lock) {
    xSemaphoreGive(g_job->lock);
  }
}

bool staOk() {
  return WiFi.status() == WL_CONNECTED;
}

void sendStaRequired() {
  g_http->send(409, "application/json", "{\"error\":\"sta_required\"}");
}

bool lanBuffersReady() {
  return g_lan_slab && g_job && g_job->hosts && g_job->open && g_lan_slab->scratch;
}

bool ensureLanBuffers() {
  if (lanBuffersReady()) {
    return true;
  }
  if (!g_lan_slab) {
    g_lan_slab = new (std::nothrow) LanSlab();
    if (!g_lan_slab) {
      return false;
    }
    g_job = &g_lan_slab->job;
    g_job->lock = xSemaphoreCreateMutex();
    if (!g_job->lock) {
      delete g_lan_slab;
      g_lan_slab = nullptr;
      g_job = nullptr;
      return false;
    }
  }

  if (!g_job->hosts) {
    g_job->hosts = static_cast<Host*>(calloc(kMaxHosts, sizeof(Host)));
  }
  if (!g_job->open) {
    g_job->open = static_cast<OpenPort*>(calloc(kMaxOpenPorts, sizeof(OpenPort)));
  }
  if (!g_lan_slab->scratch) {
    g_lan_slab->scratch_bytes = sizeof(Host) * kMaxHosts;
    g_lan_slab->scratch = static_cast<uint8_t*>(malloc(g_lan_slab->scratch_bytes));
  }

  if (!lanBuffersReady()) {
    releaseLanBuffers();
    return false;
  }
  return true;
}

void releaseLanBuffers() {
  if (!g_lan_slab) {
    return;
  }
  if (g_job) {
    g_job->watch_on = false;
    g_job->cancel = true;
  }
  const uint32_t t0 = millis();
  while (g_job && g_job->busy && millis() - t0 < 3000) {
    delay(20);
  }

  if (g_lan_slab->dyn_ports) {
    free(g_lan_slab->dyn_ports);
    g_lan_slab->dyn_ports = nullptr;
    g_lan_slab->dyn_n = 0;
  }

  lockJob();
  if (g_job) {
    free(g_job->hosts);
    g_job->hosts = nullptr;
    g_job->host_count = 0;
    free(g_job->open);
    g_job->open = nullptr;
    g_job->open_count = 0;
    g_job->cancel = false;
    g_job->busy = false;
    g_job->done = false;
    g_job->kind = JobKind::None;
  }
  free(g_lan_slab->scratch);
  g_lan_slab->scratch = nullptr;
  g_lan_slab->scratch_bytes = 0;
  unlockJob();

  if (g_job && g_job->lock) {
    vSemaphoreDelete(g_job->lock);
    g_job->lock = nullptr;
  }

  g_job = nullptr;
  delete g_lan_slab;
  g_lan_slab = nullptr;
}

}  // namespace lan_scan
