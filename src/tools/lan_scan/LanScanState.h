#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <IPAddress.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "storage/LanHost.h"

namespace lan_scan {

constexpr size_t kMaxHosts = 40;
constexpr size_t kMaxOpenPorts = 64;
constexpr size_t kMaxMdns = 32;
constexpr size_t kMaxHops = 20;

enum class JobKind : uint8_t { None, Discover, Ports, Ping, Mdns, Traceroute, Banner, HttpProbe };

/** Live scan row = shared LanHost card + UI flags. */
struct Host : storage::LanHost {
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

  Host* hosts = nullptr;
  size_t host_count = 0;

  OpenPort* open = nullptr;
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

constexpr size_t kPortTmpCap = 1024;

/** Whole LAN subsystem — heap-only while LAN tab session is open. */
struct LanSlab {
  JobState job;
  uint8_t* scratch = nullptr;
  size_t scratch_bytes = 0;
  TaskHandle_t task = nullptr;
  uint16_t* dyn_ports = nullptr;
  size_t dyn_n = 0;
  /** Custom port-list parse scratch (was BSS; lives only with LAN session). */
  uint16_t port_tmp[kPortTmpCap] = {};
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

/** Null when LAN tab closed — always null-check or call ensureLanBuffers(). */
extern JobState* g_job;
extern LanSlab* g_lan_slab;

extern const uint16_t kQuickPorts[];
extern const uint16_t kCommonPorts[];
extern const size_t kQuickPortsCount;
extern const size_t kCommonPortsCount;

void lockJob();
void unlockJob();
bool staOk();
void sendStaRequired();

/** Create LAN slab + host/open/scratch. Safe to call repeatedly. */
bool ensureLanBuffers();
/** Tear down whole LAN slab after cancel. */
void releaseLanBuffers();
bool lanBuffersReady();

}  // namespace lan_scan
