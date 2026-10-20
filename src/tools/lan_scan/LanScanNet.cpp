#include "tools/lan_scan/LanScanNet.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <cstdio>
#include <cstring>
#include <lwip/err.h>
#include <lwip/etharp.h>
#include <lwip/inet.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>

#include "ping/ping_sock.h"
#include "tools/lan_scan/LanScanState.h"

namespace lan_scan {

void formatMac(const uint8_t* m, char* out, size_t len) {
  snprintf(out, len, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

bool parseIp(const char* s, IPAddress& ip) {
  return ip.fromString(s);
}

uint32_t ipToU32(const IPAddress& ip) {
  return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | uint32_t(ip[3]);
}

IPAddress u32ToIp(uint32_t v) {
  return IPAddress(uint8_t((v >> 24) & 0xff), uint8_t((v >> 16) & 0xff), uint8_t((v >> 8) & 0xff),
                   uint8_t(v & 0xff));
}

void subnetRange(IPAddress& from, IPAddress& to) {
  const IPAddress ip = WiFi.localIP();
  const IPAddress mask = WiFi.subnetMask();
  uint32_t net = ipToU32(ip) & ipToU32(mask);
  uint32_t bcast = net | ~ipToU32(mask);
  uint32_t first = net + 1;
  uint32_t last = bcast - 1;
  if (last < first) {
    last = first;
  }
  // Cap /24-ish: max 254 hosts
  if (last - first + 1 > 254) {
    // Prefer the /24 containing our IP
    uint32_t base = ipToU32(ip) & 0xFFFFFF00u;
    first = base + 1;
    last = base + 254;
  }
  from = u32ToIp(first);
  to = u32ToIp(last);
}

struct netif* staNetif() {
  return netif_default;
}

bool arpLookup(const IPAddress& ip, uint8_t mac_out[6]) {
  struct netif* nf = staNetif();
  if (!nf) {
    return false;
  }
  ip4_addr_t tip;
  IP4_ADDR(&tip, ip[0], ip[1], ip[2], ip[3]);
  struct eth_addr* eth = nullptr;
  const ip4_addr_t* tip2 = nullptr;
  if (etharp_find_addr(nf, &tip, &eth, &tip2) >= 0 && eth) {
    memcpy(mac_out, eth->addr, 6);
    return true;
  }
  return false;
}

bool arpRequest(const IPAddress& ip) {
  struct netif* nf = staNetif();
  if (!nf) {
    return false;
  }
  ip4_addr_t tip;
  IP4_ADDR(&tip, ip[0], ip[1], ip[2], ip[3]);
  return etharp_request(nf, &tip) == ERR_OK;
}

bool arpProbe(const IPAddress& ip, uint8_t mac_out[6], int* rtt_ms) {
  // Lean path used by single-host tools: etharp_request + table lookup.
  // (Avoid WiFiUDP / ICMP — those blow stack/heap during full subnet scans.)
  const uint32_t t0 = millis();
  if (!arpRequest(ip)) {
    return false;
  }
  for (int i = 0; i < 10; ++i) {
    if (g_job.cancel) {
      return false;
    }
    if (arpLookup(ip, mac_out)) {
      if (rtt_ms) {
        *rtt_ms = static_cast<int>(millis() - t0);
      }
      return true;
    }
    delay(40);
  }
  return false;
}

struct PingCtx {
  volatile bool done = false;
  volatile bool ok = false;
  volatile int rtt = -1;
};

void onPingSuccess(esp_ping_handle_t, void* args) {
  auto* c = static_cast<PingCtx*>(args);
  if (!c) {
    return;
  }
  c->ok = true;
}

void onPingDone(esp_ping_handle_t hdl, void* args) {
  auto* c = static_cast<PingCtx*>(args);
  if (!c) {
    return;
  }
  uint32_t received = 0;
  uint32_t total_time_ms = 0;
  esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
  esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
  c->ok = received > 0;
  c->rtt = received ? static_cast<int>(total_time_ms / received) : -1;
  c->done = true;
}

bool icmpPingOnce(const IPAddress& ip, int* rtt_ms, uint32_t timeout_ms) {
  PingCtx ctx;
  ip_addr_t target;
  IP_ADDR4(&target, ip[0], ip[1], ip[2], ip[3]);
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.count = 1;
  cfg.timeout_ms = timeout_ms;
  cfg.target_addr = target;
  cfg.task_stack_size = 2048;
  cfg.task_prio = 1;
  esp_ping_callbacks_t cbs = {};
  cbs.cb_args = &ctx;
  cbs.on_ping_success = onPingSuccess;
  cbs.on_ping_timeout = nullptr;
  cbs.on_ping_end = onPingDone;
  esp_ping_handle_t ping = nullptr;
  if (esp_ping_new_session(&cfg, &cbs, &ping) != ESP_OK) {
    return false;
  }
  esp_ping_start(ping);
  const uint32_t start = millis();
  while (!ctx.done && millis() - start < timeout_ms + 500) {
    if (g_job.cancel) {
      break;
    }
    delay(10);
  }
  esp_ping_stop(ping);
  esp_ping_delete_session(ping);
  if (rtt_ms) {
    *rtt_ms = ctx.rtt;
  }
  return ctx.ok;
}

bool tcpOpen(const IPAddress& ip, uint16_t port, uint32_t timeout_ms) {
  WiFiClient c;
  c.setTimeout(timeout_ms / 1000);
  const uint32_t t0 = millis();
  if (!c.connect(ip, port, timeout_ms)) {
    return false;
  }
  // Keep briefly so we know it accepted
  (void)t0;
  c.stop();
  return true;
}

const char* serviceName(uint16_t p) {
  switch (p) {
    case 21:
      return "ftp";
    case 22:
      return "ssh";
    case 23:
      return "telnet";
    case 25:
      return "smtp";
    case 53:
      return "dns";
    case 80:
      return "http";
    case 110:
      return "pop3";
    case 139:
      return "netbios";
    case 443:
      return "https";
    case 445:
      return "smb";
    case 554:
      return "rtsp";
    case 631:
      return "ipp";
    case 3389:
      return "rdp";
    case 5900:
      return "vnc";
    case 8080:
      return "http-alt";
    case 8443:
      return "https-alt";
    case 9100:
      return "jetdirect";
    default:
      return "";
  }
}

void appendOpenPortStr(char* buf, size_t buflen, uint16_t port) {
  char tmp[8];
  snprintf(tmp, sizeof(tmp), "%u", static_cast<unsigned>(port));
  if (!buf[0]) {
    strncpy(buf, tmp, buflen - 1);
    return;
  }
  if (strlen(buf) + strlen(tmp) + 2 >= buflen) {
    return;
  }
  strcat(buf, ";");
  strcat(buf, tmp);
}

Host* findHostByIp(const char* ip) {
  for (size_t i = 0; i < g_job.host_count; ++i) {
    if (strcmp(g_job.hosts[i].ip, ip) == 0) {
      return &g_job.hosts[i];
    }
  }
  return nullptr;
}


}  // namespace lan_scan
