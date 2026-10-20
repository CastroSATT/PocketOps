#include "tools/lan_scan/LanScanState.h"

#include <WiFi.h>

namespace lan_scan {

WebServer* g_http = nullptr;
JobState g_job;
TaskHandle_t g_task = nullptr;
uint16_t* g_dyn_ports = nullptr;
size_t g_dyn_n = 0;

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
  if (g_job.lock) {
    xSemaphoreTake(g_job.lock, portMAX_DELAY);
  }
}
void unlockJob() {
  if (g_job.lock) {
    xSemaphoreGive(g_job.lock);
  }
}

bool staOk() {
  return WiFi.status() == WL_CONNECTED;
}

void sendStaRequired() {
  g_http->send(409, "application/json", "{\"error\":\"sta_required\"}");
}

}  // namespace lan_scan
