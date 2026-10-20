#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace storage {

struct LanHostRec {
  char ip[16] = {};
  char mac[18] = {};
  char hostname[40] = {};
  char vendor[24] = {};
  char notes[64] = {};
  char open_ports[96] = {};
  int rtt_ms = -1;
};

// On-device (LittleFS) LAN scan profiles keyed by Wi‑Fi AP BSSID.
// Max 100; oldest by last_used evicted. Not stored on SD / not in backup.
class LanProfiles {
 public:
  static LanProfiles& instance();

  static constexpr size_t kMaxProfiles = 100;
  static constexpr size_t kMaxHosts = 40;  // hosts per saved LAN profile
  static constexpr size_t kBssidLen = 18;
  static constexpr size_t kSsidLen = 33;

  bool begin();  // ensure /config/lan_profiles

  size_t count() const { return count_; }
  const char* bssidAt(size_t i) const;
  const char* ssidAt(size_t i) const;
  uint32_t lastUsedAt(size_t i) const;

  bool loadIndex();
  bool has(const char* bssid) const;
  int indexOf(const char* bssid) const;

  // Load hosts for BSSID into out[0..*out_count). Returns false if missing.
  bool loadHosts(const char* bssid, LanHostRec* out, size_t max_out, size_t* out_count,
                 char* ssid_out, size_t ssid_len);

  // Save / update profile for bssid. Evicts oldest if at capacity and new.
  bool saveProfile(const char* bssid, const char* ssid, const LanHostRec* hosts, size_t host_count);

  bool setHostNote(const char* bssid, const char* host_mac, const char* note);

  // Wipe profile for bssid (current network clear).
  bool clearProfile(const char* bssid);
  bool removeAt(size_t index);

  // Wipe every LAN profile + index.
  bool clearAll();

  void touch(const char* bssid);

 private:
  LanProfiles() = default;
  bool ensureDir();
  bool saveIndex();
  void normalizeBssid(const char* in, char* out, size_t out_len) const;
  void fileFor(const char* bssid_norm, char* path, size_t path_len) const;
  bool evictOldest();

  size_t count_ = 0;
  char bssids_[kMaxProfiles][kBssidLen] = {};
  char ssids_[kMaxProfiles][kSsidLen] = {};
  uint32_t last_used_[kMaxProfiles] = {};
};

}  // namespace storage
