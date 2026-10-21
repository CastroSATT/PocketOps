#include "storage/LanProfiles.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "util/TextUtil.h"

namespace storage {
namespace {

constexpr const char* kDir = "/config/lan_profiles";
constexpr const char* kIndex = "/config/lan_profiles/index.txt";

using util::trimInPlace;

}  // namespace

LanProfiles& LanProfiles::instance() {
  static LanProfiles s;
  return s;
}

bool LanProfiles::ensureDir() {
  if (LittleFS.exists(kDir)) {
    return true;
  }
  return LittleFS.mkdir(kDir);
}

bool LanProfiles::begin() {
  if (!ensureDir()) {
    return false;
  }
  return loadIndex();
}

void LanProfiles::normalizeBssid(const char* in, char* out, size_t out_len) const {
  if (!out || out_len < 18) {
    return;
  }
  out[0] = '\0';
  if (!in) {
    return;
  }
  char hex[13] = {};
  size_t h = 0;
  for (size_t i = 0; in[i] && h < 12; ++i) {
    const char c = in[i];
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
      hex[h++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
  }
  hex[h] = '\0';
  if (h != 12) {
    return;
  }
  snprintf(out, out_len, "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c", hex[0], hex[1], hex[2], hex[3], hex[4],
           hex[5], hex[6], hex[7], hex[8], hex[9], hex[10], hex[11]);
}

void LanProfiles::fileFor(const char* bssid_norm, char* path, size_t path_len) const {
  char key[13] = {};
  size_t k = 0;
  for (size_t i = 0; bssid_norm && bssid_norm[i] && k < 12; ++i) {
    if (bssid_norm[i] != ':' && bssid_norm[i] != '-') {
      key[k++] = bssid_norm[i];
    }
  }
  key[k] = '\0';
  snprintf(path, path_len, "%s/%s.txt", kDir, key);
}

const char* LanProfiles::bssidAt(size_t i) const {
  return i < count_ ? bssids_[i] : "";
}
const char* LanProfiles::ssidAt(size_t i) const {
  return i < count_ ? ssids_[i] : "";
}
uint32_t LanProfiles::lastUsedAt(size_t i) const {
  return i < count_ ? last_used_[i] : 0;
}

int LanProfiles::indexOf(const char* bssid) const {
  char norm[kBssidLen];
  normalizeBssid(bssid, norm, sizeof(norm));
  if (!norm[0]) {
    return -1;
  }
  for (size_t i = 0; i < count_; ++i) {
    if (strcasecmp(bssids_[i], norm) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool LanProfiles::has(const char* bssid) const {
  return indexOf(bssid) >= 0;
}

bool LanProfiles::loadIndex() {
  count_ = 0;
  if (!ensureDir()) {
    return false;
  }
  if (!LittleFS.exists(kIndex)) {
    return true;
  }
  File f = LittleFS.open(kIndex, "r");
  if (!f) {
    return false;
  }
  while (f.available() && count_ < kMaxProfiles) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length() || line[0] == '#') {
      continue;
    }
    // bssid|ssid|last_used
    const int p1 = line.indexOf('|');
    if (p1 < 0) {
      continue;
    }
    const int p2 = line.indexOf('|', p1 + 1);
    if (p2 < 0) {
      continue;
    }
    String bs = line.substring(0, p1);
    String ss = line.substring(p1 + 1, p2);
    String lu = line.substring(p2 + 1);
    bs.trim();
    ss.trim();
    normalizeBssid(bs.c_str(), bssids_[count_], kBssidLen);
    if (!bssids_[count_][0]) {
      continue;
    }
    strncpy(ssids_[count_], ss.c_str(), kSsidLen - 1);
    ssids_[count_][kSsidLen - 1] = '\0';
    last_used_[count_] = static_cast<uint32_t>(lu.toInt());
    ++count_;
  }
  f.close();
  return true;
}

bool LanProfiles::saveIndex() {
  if (!ensureDir()) {
    return false;
  }
  File f = LittleFS.open(kIndex, "w");
  if (!f) {
    return false;
  }
  f.println("# bssid|ssid|last_used");
  for (size_t i = 0; i < count_; ++i) {
    f.printf("%s|%s|%lu\n", bssids_[i], ssids_[i], static_cast<unsigned long>(last_used_[i]));
  }
  f.close();
  return true;
}

bool LanProfiles::evictOldest() {
  if (count_ == 0) {
    return false;
  }
  size_t oldest = 0;
  for (size_t i = 1; i < count_; ++i) {
    if (last_used_[i] < last_used_[oldest]) {
      oldest = i;
    }
  }
  return removeAt(oldest);
}

bool LanProfiles::removeAt(size_t index) {
  if (index >= count_) {
    return false;
  }
  char path[64];
  fileFor(bssids_[index], path, sizeof(path));
  if (LittleFS.exists(path)) {
    LittleFS.remove(path);
  }
  for (size_t i = index; i + 1 < count_; ++i) {
    memcpy(bssids_[i], bssids_[i + 1], kBssidLen);
    memcpy(ssids_[i], ssids_[i + 1], kSsidLen);
    last_used_[i] = last_used_[i + 1];
  }
  --count_;
  return saveIndex();
}

bool LanProfiles::clearProfile(const char* bssid) {
  const int idx = indexOf(bssid);
  if (idx < 0) {
    return true;
  }
  return removeAt(static_cast<size_t>(idx));
}

bool LanProfiles::clearAll() {
  while (count_ > 0) {
    if (!removeAt(0)) {
      count_ = 0;
      break;
    }
  }
  const bool ok = saveIndex();
  return ok;
}

void LanProfiles::touch(const char* bssid) {
  const int idx = indexOf(bssid);
  if (idx < 0) {
    return;
  }
  last_used_[idx] = static_cast<uint32_t>(time(nullptr));
  if (last_used_[idx] < 100000) {
    last_used_[idx] = millis() / 1000;
  }
  saveIndex();
}

bool LanProfiles::loadHosts(const char* bssid, LanHost* out, size_t max_out, size_t* out_count,
                            char* ssid_out, size_t ssid_len) {
  if (out_count) {
    *out_count = 0;
  }
  char norm[kBssidLen];
  normalizeBssid(bssid, norm, sizeof(norm));
  if (!norm[0] || !out || !max_out) {
    return false;
  }
  const int idx = indexOf(norm);
  if (idx < 0) {
    return false;
  }
  if (ssid_out && ssid_len) {
    strncpy(ssid_out, ssids_[idx], ssid_len - 1);
    ssid_out[ssid_len - 1] = '\0';
  }
  char path[64];
  fileFor(norm, path, sizeof(path));
  File f = LittleFS.open(path, "r");
  if (!f) {
    return false;
  }
  size_t n = 0;
  while (f.available() && n < max_out) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.startsWith("H|")) {
      continue;
    }
    // H|ip|mac|hostname|vendor|rtt|notes|ports
    String rest = line.substring(2);
    String parts[7];
    int start = 0;
    int pi = 0;
    for (; pi < 7; ++pi) {
      const int bar = rest.indexOf('|', start);
      if (bar < 0) {
        parts[pi] = rest.substring(start);
        ++pi;
        break;
      }
      parts[pi] = rest.substring(start, bar);
      start = bar + 1;
    }
    LanHost& h = out[n];
    h = {};
    strncpy(h.ip, parts[0].c_str(), sizeof(h.ip) - 1);
    strncpy(h.mac, parts[1].c_str(), sizeof(h.mac) - 1);
    strncpy(h.hostname, parts[2].c_str(), sizeof(h.hostname) - 1);
    strncpy(h.vendor, parts[3].c_str(), sizeof(h.vendor) - 1);
    h.rtt_ms = parts[4].toInt();
    if (pi > 5) {
      strncpy(h.notes, parts[5].c_str(), sizeof(h.notes) - 1);
    }
    if (pi > 6) {
      strncpy(h.open_ports, parts[6].c_str(), sizeof(h.open_ports) - 1);
    }
    ++n;
  }
  f.close();
  if (out_count) {
    *out_count = n;
  }
  touch(norm);
  return true;
}

bool LanProfiles::saveProfile(const char* bssid, const char* ssid, const LanHost* hosts,
                              size_t host_count) {
  char norm[kBssidLen];
  normalizeBssid(bssid, norm, sizeof(norm));
  if (!norm[0]) {
    return false;
  }
  if (!ensureDir()) {
    return false;
  }
  int idx = indexOf(norm);
  if (idx < 0) {
    if (count_ >= kMaxProfiles) {
      if (!evictOldest()) {
        return false;
      }
    }
    idx = static_cast<int>(count_);
    strncpy(bssids_[count_], norm, kBssidLen - 1);
    ++count_;
  }
  if (ssid && ssid[0]) {
    strncpy(ssids_[idx], ssid, kSsidLen - 1);
    ssids_[idx][kSsidLen - 1] = '\0';
  }
  last_used_[idx] = static_cast<uint32_t>(time(nullptr));
  if (last_used_[idx] < 100000) {
    last_used_[idx] = millis() / 1000;
  }

  char path[64];
  fileFor(norm, path, sizeof(path));
  File f = LittleFS.open(path, "w");
  if (!f) {
    return false;
  }
  f.printf("SSID=%s\n", ssids_[idx]);
  f.printf("USED=%lu\n", static_cast<unsigned long>(last_used_[idx]));
  const size_t n = host_count > kMaxHosts ? kMaxHosts : host_count;
  for (size_t i = 0; i < n && hosts; ++i) {
    const LanHost& h = hosts[i];
    f.printf("H|%s|%s|%s|%s|%d|%s|%s\n", h.ip, h.mac, h.hostname, h.vendor, h.rtt_ms, h.notes,
             h.open_ports);
  }
  f.close();
  return saveIndex();
}

bool LanProfiles::setHostNote(const char* bssid, const char* host_mac, const char* note) {
  // Heap — avoid ~10KB stack spike without permanent BSS.
  LanHost* hosts = static_cast<LanHost*>(malloc(sizeof(LanHost) * kMaxHosts));
  if (!hosts) {
    return false;
  }
  size_t n = 0;
  char ssid[kSsidLen] = {};
  if (!loadHosts(bssid, hosts, kMaxHosts, &n, ssid, sizeof(ssid))) {
    free(hosts);
    // Create empty profile with one host note placeholder.
    if (!host_mac || !host_mac[0]) {
      return false;
    }
    LanHost one = {};
    strncpy(one.mac, host_mac, sizeof(one.mac) - 1);
    if (note) {
      strncpy(one.notes, note, sizeof(one.notes) - 1);
    }
    return saveProfile(bssid, WiFi.SSID().c_str(), &one, 1);
  }
  char macn[18] = {};
  normalizeBssid(host_mac, macn, sizeof(macn));
  bool found = false;
  for (size_t i = 0; i < n; ++i) {
    char hm[18] = {};
    normalizeBssid(hosts[i].mac, hm, sizeof(hm));
    if (strcasecmp(hm, macn) == 0) {
      strncpy(hosts[i].notes, note ? note : "", sizeof(hosts[i].notes) - 1);
      found = true;
      break;
    }
  }
  if (!found && n < kMaxHosts && host_mac) {
    strncpy(hosts[n].mac, host_mac, sizeof(hosts[n].mac) - 1);
    strncpy(hosts[n].notes, note ? note : "", sizeof(hosts[n].notes) - 1);
    ++n;
  }
  const bool ok = saveProfile(bssid, ssid, hosts, n);
  free(hosts);
  return ok;
}

}  // namespace storage
