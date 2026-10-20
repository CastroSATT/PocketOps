#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <stddef.h>
#include <stdint.h>

#include "tools/lan_scan/LanScanState.h"

namespace lan_scan {

void formatMac(const uint8_t* m, char* out, size_t len);
bool parseIp(const char* s, IPAddress& ip);
uint32_t ipToU32(const IPAddress& ip);
IPAddress u32ToIp(uint32_t v);
void subnetRange(IPAddress& from, IPAddress& to);
/** Send one ARP request (lwIP etharp_request). */
bool arpRequest(const IPAddress& ip);
/** Read MAC from lwIP ARP table if present. */
bool arpLookup(const IPAddress& ip, uint8_t mac_out[6]);
bool arpProbe(const IPAddress& ip, uint8_t mac_out[6], int* rtt_ms);
bool icmpPingOnce(const IPAddress& ip, int* rtt_ms, uint32_t timeout_ms = 400);
bool tcpOpen(const IPAddress& ip, uint16_t port, uint32_t timeout_ms = 250);
const char* serviceName(uint16_t p);
void appendOpenPortStr(char* buf, size_t buflen, uint16_t port);
Host* findHostByIp(const char* ip);

}  // namespace lan_scan
