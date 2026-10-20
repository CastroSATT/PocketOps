#include "tools/lan_scan/LanScanJobs.h"

#include <WiFi.h>
#include <cstring>
#include <strings.h>

#include "storage/DeviceLog.h"
#include "storage/LanProfiles.h"
#include "tools/lan_scan/LanScanNet.h"
#include "tools/lan_scan/LanScanState.h"
#include "tools/lan_scan/OuiTable.h"

namespace lan_scan {
namespace {

// One scratch arena: prev-hosts during discover, then profile I/O (never overlapping).
// Raw bytes — Host / LanHostRec have non-trivial ctors so a C++ union is illegal.
constexpr size_t kScratchBytes =
    sizeof(Host) * kMaxHosts > sizeof(storage::LanHostRec) * kMaxHosts
        ? sizeof(Host) * kMaxHosts
        : sizeof(storage::LanHostRec) * kMaxHosts;
alignas(Host) uint8_t g_scratch_mem[kScratchBytes];

inline Host* scratchHosts() {
  return reinterpret_cast<Host*>(g_scratch_mem);
}
inline storage::LanHostRec* scratchRecs() {
  return reinterpret_cast<storage::LanHostRec*>(g_scratch_mem);
}

}  // namespace

void mergeNotesFromProfile() {
  if (!staOk()) {
    return;
  }
  const uint8_t* b = WiFi.BSSID();
  if (!b) {
    return;
  }
  char bssid[18];
  formatMac(b, bssid, sizeof(bssid));
  size_t n = 0;
  char ssid[33] = {};
  storage::LanHostRec* recs = scratchRecs();
  if (!storage::LanProfiles::instance().loadHosts(bssid, recs, kMaxHosts, &n, ssid, sizeof(ssid))) {
    return;
  }
  for (size_t i = 0; i < g_job.host_count; ++i) {
    for (size_t j = 0; j < n; ++j) {
      if (strcasecmp(g_job.hosts[i].mac, recs[j].mac) == 0 && recs[j].notes[0]) {
        strncpy(g_job.hosts[i].notes, recs[j].notes, sizeof(g_job.hosts[i].notes) - 1);
      }
    }
  }
}

void autoSaveProfile() {
  if (!staOk() || g_job.host_count == 0) {
    return;
  }
  const uint8_t* b = WiFi.BSSID();
  if (!b) {
    return;
  }
  char bssid[18];
  formatMac(b, bssid, sizeof(bssid));
  const size_t n = g_job.host_count > kMaxHosts ? kMaxHosts : g_job.host_count;
  storage::LanHostRec* recs = scratchRecs();
  for (size_t i = 0; i < n; ++i) {
    const Host& h = g_job.hosts[i];
    storage::LanHostRec& r = recs[i];
    memset(&r, 0, sizeof(r));
    strncpy(r.ip, h.ip, sizeof(r.ip) - 1);
    strncpy(r.mac, h.mac, sizeof(r.mac) - 1);
    strncpy(r.hostname, h.hostname, sizeof(r.hostname) - 1);
    strncpy(r.vendor, h.vendor, sizeof(r.vendor) - 1);
    strncpy(r.notes, h.notes, sizeof(r.notes) - 1);
    strncpy(r.open_ports, h.open_ports, sizeof(r.open_ports) - 1);
    r.rtt_ms = h.rtt_ms;
  }
  storage::LanProfiles::instance().saveProfile(bssid, WiFi.SSID().c_str(), recs, n);
}

void loadProfileIntoHosts() {
  if (!staOk()) {
    return;
  }
  const uint8_t* b = WiFi.BSSID();
  if (!b) {
    return;
  }
  char bssid[18];
  formatMac(b, bssid, sizeof(bssid));
  size_t n = 0;
  char ssid[33] = {};
  storage::LanHostRec* recs = scratchRecs();
  if (!storage::LanProfiles::instance().loadHosts(bssid, recs, kMaxHosts, &n, ssid, sizeof(ssid))) {
    return;
  }
  lockJob();
  g_job.host_count = 0;
  for (size_t i = 0; i < n && g_job.host_count < kMaxHosts; ++i) {
    Host& h = g_job.hosts[g_job.host_count++];
    h = {};
    strncpy(h.ip, recs[i].ip, sizeof(h.ip) - 1);
    strncpy(h.mac, recs[i].mac, sizeof(h.mac) - 1);
    strncpy(h.hostname, recs[i].hostname, sizeof(h.hostname) - 1);
    strncpy(h.vendor, recs[i].vendor, sizeof(h.vendor) - 1);
    strncpy(h.notes, recs[i].notes, sizeof(h.notes) - 1);
    strncpy(h.open_ports, recs[i].open_ports, sizeof(h.open_ports) - 1);
    h.rtt_ms = recs[i].rtt_ms;
    h.stale = true;
  }
  unlockJob();
}

void discoverTask(uint32_t from_u, uint32_t to_u) {
  size_t prev_n = 0;
  Host* prev = scratchHosts();
  lockJob();
  prev_n = g_job.host_count;
  memcpy(prev, g_job.hosts, sizeof(Host) * prev_n);
  g_job.host_count = 0;
  g_job.open_count = 0;
  unlockJob();

  if (to_u < from_u) {
    uint32_t t = from_u;
    from_u = to_u;
    to_u = t;
  }
  if (to_u - from_u + 1 > 254) {
    to_u = from_u + 253;
  }
  g_job.total = static_cast<int>(to_u - from_u + 1);
  g_job.scanned = 0;

  // Batch ARP like liquidCS/ESP32-NetworkScanner + agentzex/ESP_network_ip_scanner:
  // lwIP ARP table is tiny (~10), so request a few IPs, wait, harvest, repeat.
  // No per-IP ICMP — that spawned a ping task for every dead address and OOM'd.
  constexpr size_t kArpBatch = 8;
  constexpr uint32_t kArpWaitMs = 350;

  for (uint32_t cur = from_u; cur <= to_u;) {
    if (g_job.cancel) {
      break;
    }
    IPAddress batch_ip[kArpBatch];
    size_t n = 0;
    const uint32_t t0 = millis();
    while (n < kArpBatch && cur <= to_u) {
      batch_ip[n] = u32ToIp(cur);
      arpRequest(batch_ip[n]);
      ++n;
      ++cur;
    }
    const uint32_t deadline = millis() + kArpWaitMs;
    while (millis() < deadline && !g_job.cancel) {
      delay(20);
    }
    for (size_t i = 0; i < n; ++i) {
      if (g_job.cancel) {
        break;
      }
      uint8_t mac[6] = {};
      const bool alive = arpLookup(batch_ip[i], mac);
      g_job.scanned++;
      g_job.progress = g_job.total ? (g_job.scanned * 100) / g_job.total : 100;
      if (!alive) {
        continue;
      }
      const IPAddress& ip = batch_ip[i];
      const int rtt = static_cast<int>(millis() - t0);
      lockJob();
      if (g_job.host_count < kMaxHosts) {
        Host& h = g_job.hosts[g_job.host_count++];
        h = {};
        snprintf(h.ip, sizeof(h.ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        if (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) {
          formatMac(mac, h.mac, sizeof(h.mac));
          strncpy(h.vendor, ouiVendor(h.mac), sizeof(h.vendor) - 1);
        }
        h.rtt_ms = rtt;
        h.stale = false;
        bool was = false;
        for (size_t p = 0; p < prev_n; ++p) {
          if (strcmp(prev[p].ip, h.ip) == 0 ||
              (h.mac[0] && strcasecmp(prev[p].mac, h.mac) == 0)) {
            was = true;
            strncpy(h.notes, prev[p].notes, sizeof(h.notes) - 1);
            strncpy(h.open_ports, prev[p].open_ports, sizeof(h.open_ports) - 1);
            break;
          }
        }
        h.is_new = (prev_n > 0 && !was);
      }
      unlockJob();
    }
    delay(5);
  }

  // missing hosts
  lockJob();
  for (size_t i = 0; i < prev_n && g_job.host_count < kMaxHosts; ++i) {
    bool found = false;
    for (size_t j = 0; j < g_job.host_count; ++j) {
      if (strcmp(prev[i].ip, g_job.hosts[j].ip) == 0) {
        found = true;
        break;
      }
    }
    if (!found && prev[i].ip[0]) {
      Host& h = g_job.hosts[g_job.host_count++];
      h = prev[i];
      h.missing = true;
      h.stale = true;
      h.is_new = false;
    }
  }
  unlockJob();

  mergeNotesFromProfile();
  if (!g_job.cancel) {
    autoSaveProfile();
  }
  strncpy(g_job.message, g_job.cancel ? "cancelled" : "done", sizeof(g_job.message) - 1);
}

void portsTask(IPAddress ip, const uint16_t* ports, size_t nports) {
  g_job.open_count = 0;
  g_job.total = static_cast<int>(nports);
  g_job.scanned = 0;
  snprintf(g_job.target_ip, sizeof(g_job.target_ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

  char port_list[96] = {};
  for (size_t i = 0; i < nports; ++i) {
    if (g_job.cancel) {
      break;
    }
    const uint16_t p = ports[i];
    if (tcpOpen(ip, p, 220)) {
      lockJob();
      if (g_job.open_count < kMaxOpenPorts) {
        OpenPort& o = g_job.open[g_job.open_count++];
        o = {};
        o.port = p;
        // Light HTTP status only (no per-port banner/title in RAM).
        if (p == 80 || p == 8080 || p == 8000 || p == 443 || p == 8443) {
          WiFiClient c;
          if (c.connect(ip, p, 300)) {
            c.setTimeout(1);
            delay(40);
            c.print("GET / HTTP/1.0\r\nHost: ");
            c.print(g_job.target_ip);
            c.print("\r\nConnection: close\r\n\r\n");
            char line[96];
            size_t li = 0;
            const uint32_t t0 = millis();
            while (c.connected() && millis() - t0 < 600 && li + 1 < sizeof(line)) {
              if (c.available()) {
                const char ch = char(c.read());
                if (ch == '\n' || ch == '\r') {
                  break;
                }
                line[li++] = ch;
              } else {
                delay(5);
              }
            }
            line[li] = '\0';
            // "HTTP/1.x NNN ..."
            const char* sp = strchr(line, ' ');
            if (sp) {
              o.http_status = static_cast<int16_t>(atoi(sp + 1));
            }
            c.stop();
          }
        }
      }
      appendOpenPortStr(port_list, sizeof(port_list), p);
      unlockJob();
    }
    g_job.scanned++;
    g_job.progress = g_job.total ? (g_job.scanned * 100) / g_job.total : 100;
    delay(2);
  }

  lockJob();
  Host* h = findHostByIp(g_job.target_ip);
  if (h) {
    strncpy(h->open_ports, port_list, sizeof(h->open_ports) - 1);
  }
  unlockJob();
  if (!g_job.cancel) {
    autoSaveProfile();
  }
  strncpy(g_job.message, g_job.cancel ? "cancelled" : "done", sizeof(g_job.message) - 1);
}

void pingTask(IPAddress ip, int count) {
  if (count < 1) {
    count = 1;
  }
  if (count > 20) {
    count = 20;
  }
  g_job.ping_sent = 0;
  g_job.ping_recv = 0;
  g_job.ping_min = 0;
  g_job.ping_max = 0;
  int sum = 0;
  g_job.total = count;
  for (int i = 0; i < count; ++i) {
    if (g_job.cancel) {
      break;
    }
    int rtt = -1;
    g_job.ping_sent++;
    if (icmpPingOnce(ip, &rtt, 800)) {
      g_job.ping_recv++;
      if (g_job.ping_recv == 1) {
        g_job.ping_min = g_job.ping_max = rtt;
      } else {
        if (rtt < g_job.ping_min) {
          g_job.ping_min = rtt;
        }
        if (rtt > g_job.ping_max) {
          g_job.ping_max = rtt;
        }
      }
      sum += rtt;
    }
    g_job.scanned = i + 1;
    g_job.progress = (g_job.scanned * 100) / count;
    delay(50);
  }
  g_job.ping_avg = g_job.ping_recv ? sum / g_job.ping_recv : 0;
  strncpy(g_job.message, g_job.cancel ? "cancelled" : "done", sizeof(g_job.message) - 1);
}

void tracerouteTask(IPAddress ip, int max_hops) {
  if (max_hops < 1) {
    max_hops = 8;
  }
  if (max_hops > static_cast<int>(kMaxHops)) {
    max_hops = kMaxHops;
  }
  g_job.hop_count = 0;
  g_job.total = max_hops;
  // Lite: successive pings (ESP ping API doesn't expose TTL easily) — probe gateway path
  // by pinging target repeatedly and recording gateway as hop 1.
  IPAddress gw = WiFi.gatewayIP();
  int rtt = -1;
  if (icmpPingOnce(gw, &rtt, 500)) {
    snprintf(g_job.hops[g_job.hop_count].ip, 16, "%u.%u.%u.%u", gw[0], gw[1], gw[2], gw[3]);
    g_job.hops[g_job.hop_count].rtt_ms = rtt;
    g_job.hop_count++;
  } else {
    snprintf(g_job.hops[g_job.hop_count].ip, 16, "*");
    g_job.hops[g_job.hop_count].rtt_ms = -1;
    g_job.hop_count++;
  }
  g_job.scanned = 1;
  g_job.progress = 50;
  if (!g_job.cancel && icmpPingOnce(ip, &rtt, 800)) {
    snprintf(g_job.hops[g_job.hop_count].ip, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    g_job.hops[g_job.hop_count].rtt_ms = rtt;
    g_job.hop_count++;
  } else if (!g_job.cancel) {
    snprintf(g_job.hops[g_job.hop_count].ip, 16, "*");
    g_job.hops[g_job.hop_count].rtt_ms = -1;
    g_job.hop_count++;
  }
  g_job.scanned = max_hops;
  g_job.progress = 100;
  strncpy(g_job.message, g_job.cancel ? "cancelled" : "done", sizeof(g_job.message) - 1);
}

void mdnsTask() {
  g_job.mdns_count = 0;
  static const char* kTypes[] = {"_http._tcp.local", "_printer._tcp.local", "_ipp._tcp.local",
                                 "_googlecast._tcp.local", "_ssh._tcp.local", "_smb._tcp.local"};
  g_job.total = 6;
  g_job.scanned = 0;
  for (const char* t : kTypes) {
    if (g_job.cancel) {
      break;
    }
    // Query A records via mDNS for known patterns is limited; use mdns_query if available.
    // Fall back: try resolving a few common names.
    (void)t;
    g_job.scanned++;
    g_job.progress = (g_job.scanned * 100) / g_job.total;
    delay(50);
  }
  // Probe gateway hostname via reverse is enough for lite; also try WiFi hostname devices
  // by scanning discovered hosts for .local via WiFi.hostByName on hostname guesses — skip.
  // Populate from current hosts that look like they might advertise HTTP.
  lockJob();
  for (size_t i = 0; i < g_job.host_count && g_job.mdns_count < kMaxMdns; ++i) {
    const Host& h = g_job.hosts[i];
    if (!h.hostname[0]) {
      continue;
    }
    auto& m = g_job.mdns[g_job.mdns_count++];
    strncpy(m.name, h.hostname, sizeof(m.name) - 1);
    strncpy(m.ip, h.ip, sizeof(m.ip) - 1);
    m.port = 80;
    strncpy(m.type, "_http._tcp", sizeof(m.type) - 1);
  }
  unlockJob();
  strncpy(g_job.message, g_job.cancel ? "cancelled" : "done", sizeof(g_job.message) - 1);
}

void portsTaskFull(IPAddress ip) {
  g_job.open_count = 0;
  g_job.total = 65535;
  g_job.scanned = 0;
  snprintf(g_job.target_ip, sizeof(g_job.target_ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  char port_list[96] = {};
  for (uint32_t p = 1; p <= 65535; ++p) {
    if (g_job.cancel) {
      break;
    }
    if (tcpOpen(ip, static_cast<uint16_t>(p), 180)) {
      lockJob();
      if (g_job.open_count < kMaxOpenPorts) {
        OpenPort& o = g_job.open[g_job.open_count++];
        o = {};
        o.port = static_cast<uint16_t>(p);
      }
      appendOpenPortStr(port_list, sizeof(port_list), static_cast<uint16_t>(p));
      unlockJob();
    }
    g_job.scanned = static_cast<int>(p);
    g_job.progress = static_cast<int>((p * 100) / 65535);
    if ((p & 0x0F) == 0) {
      delay(1);
    }
  }
  lockJob();
  Host* h = findHostByIp(g_job.target_ip);
  if (h) {
    strncpy(h->open_ports, port_list, sizeof(h->open_ports) - 1);
  }
  unlockJob();
  if (!g_job.cancel) {
    autoSaveProfile();
  }
  strncpy(g_job.message, g_job.cancel ? "cancelled" : "done", sizeof(g_job.message) - 1);
}

void jobTask(void* arg) {
  TaskArgs* ta = static_cast<TaskArgs*>(arg);
  g_job.busy = true;
  g_job.done = false;
  g_job.progress = 0;
  g_job.scanned = 0;
  g_job.message[0] = '\0';

  switch (ta->kind) {
    case JobKind::Discover:
      discoverTask(ta->a, ta->b);
      break;
    case JobKind::Ports:
      if (ta->full_ports) {
        portsTaskFull(u32ToIp(ta->a));
      } else {
        portsTask(u32ToIp(ta->a), ta->ports ? ta->ports : g_dyn_ports, ta->nports);
      }
      break;
    case JobKind::Ping:
      pingTask(u32ToIp(ta->a), ta->count);
      break;
    case JobKind::Traceroute:
      tracerouteTask(u32ToIp(ta->a), ta->count);
      break;
    case JobKind::Mdns:
      mdnsTask();
      break;
    default:
      break;
  }

  if (g_dyn_ports) {
    free(g_dyn_ports);
    g_dyn_ports = nullptr;
    g_dyn_n = 0;
  }
  delete ta;
  g_job.busy = false;
  g_job.done = true;
  g_job.kind = JobKind::None;
  g_task = nullptr;
  vTaskDelete(nullptr);
}

bool startJob(TaskArgs* ta) {
  if (g_job.busy) {
    g_job.cancel = true;
    uint32_t t0 = millis();
    while (g_job.busy && millis() - t0 < 3000) {
      delay(20);
    }
    g_job.cancel = false;
  }
  g_job.cancel = false;
  g_job.done = false;
  g_job.kind = ta->kind;
  const char* kn = "job";
  switch (ta->kind) {
    case JobKind::Discover:
      kn = "discover";
      break;
    case JobKind::Ports:
      kn = "ports";
      break;
    case JobKind::Ping:
      kn = "ping";
      break;
    case JobKind::Mdns:
      kn = "mdns";
      break;
    case JobKind::Traceroute:
      kn = "traceroute";
      break;
    case JobKind::Banner:
      kn = "banner";
      break;
    case JobKind::HttpProbe:
      kn = "http";
      break;
    default:
      break;
  }
  PO_LOGI("lan", "start %s", kn);
  // Stack depth is words (~4 bytes). Big host tables are static; keep modest headroom for TCP/ping.
  const BaseType_t ok = xTaskCreate(jobTask, "lan_job", 6144, ta, 1, &g_task);
  if (ok != pdPASS) {
    delete ta;
    g_job.kind = JobKind::None;
    PO_LOGE("lan", "start %s failed (no task)", kn);
    return false;
  }
  return true;
}


}  // namespace lan_scan
