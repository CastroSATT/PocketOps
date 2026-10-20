#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <IPAddress.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace lan_scan {

constexpr size_t kMaxHosts = 40;
constexpr size_t kMaxOpenPorts = 64;
constexpr size_t kMaxMdns = 32;
constexpr size_t kMaxHops = 20;

enum class JobKind : uint8_t { None, Discover, Ports, Ping, Mdns, Traceroute, Banner, HttpProbe };

struct Host {
  char ip[16] = {};
  char mac[18] = {};
  char hostname[40] = {};
  char vendor[24] = {};
  char notes[64] = {};
  char open_ports[96] = {};
  int rtt_ms = -1;
  bool stale = false;
  bool is_new = false;
  bool missing = false;
};

// Compact: banners/titles are not kept for every open port (saves ~10KB+).
struct OpenPort {
  uint16_t port = 0;
  int16_t http_status = 0;
};

struct JobState {
  volatile JobKind kind = JobKind::None;
  volatile bool busy = false;
  volatile bool cancel = false;
  volatile bool done = false;
  volatile int progress = 0;
  volatile int scanned = 0;
  volatile int total = 0;
  char target_ip[16] = {};
  char message[80] = {};

  Host hosts[kMaxHosts];
  size_t host_count = 0;

  OpenPort open[kMaxOpenPorts];
  size_t open_count = 0;

  int ping_sent = 0;
  int ping_recv = 0;
  int ping_min = 0;
  int ping_max = 0;
  int ping_avg = 0;

  struct Hop {
    char ip[16];
    int rtt_ms;
  } hops[kMaxHops];
  size_t hop_count = 0;

  struct MdnsSvc {
    char name[48];
    char ip[16];
    uint16_t port;
    char type[40];
  } mdns[kMaxMdns];
  size_t mdns_count = 0;

  bool watch_on = false;
  uint32_t watch_interval_ms = 300000;
  uint32_t watch_last_ms = 0;

  SemaphoreHandle_t lock = nullptr;
};

struct TaskArgs {
  JobKind kind;
  uint32_t a;
  uint32_t b;
  uint16_t* ports;
  size_t nports;
  int count;
  bool full_ports = false;
};

extern WebServer* g_http;
extern JobState g_job;
extern TaskHandle_t g_task;
extern uint16_t* g_dyn_ports;
extern size_t g_dyn_n;
extern const uint16_t kQuickPorts[];
extern const uint16_t kCommonPorts[];
extern const size_t kQuickPortsCount;
extern const size_t kCommonPortsCount;

void lockJob();
void unlockJob();
bool staOk();
void sendStaRequired();

}  // namespace lan_scan
