#pragma once

#include <stddef.h>

namespace storage {

// Saved Wi-Fi networks on LittleFS (/config/wifi_profiles.ini).
// auto_join profiles are tried when SoftAP+join / STA starts; SoftAP is fallback.
class WifiProfiles {
 public:
  static WifiProfiles& instance();

  static constexpr size_t kMaxProfiles = 100;
  static constexpr size_t kSsidLen = 33;
  static constexpr size_t kPassLen = 65;
  static constexpr size_t kBssidLen = 18;  // AA:BB:CC:DD:EE:FF

  bool load();
  bool save();

  size_t count() const { return count_; }
  const char* ssid(size_t index) const;
  const char* pass(size_t index) const;
  const char* bssid(size_t index) const;
  bool autoJoin(size_t index) const;

  // Find password for SSID (case-sensitive). Returns false if unknown.
  bool findPass(const char* ssid, char* pass_out, size_t pass_len) const;
  bool hasSsid(const char* ssid) const;
  bool isAutoJoin(const char* ssid) const;

  // Add or update. bssid optional (keeps existing if blank).
  // auto_join_set: if true, apply auto_join value; if false, keep existing (default true for new).
  bool upsert(const char* ssid, const char* pass, const char* bssid = nullptr,
              bool auto_join_set = false, bool auto_join = true);
  bool setAutoJoin(const char* ssid, bool on);
  // Remove by SSID, then save.
  bool remove(const char* ssid);

 private:
  WifiProfiles() = default;
  void clear();
  bool ensureFile();
  int indexOf(const char* ssid) const;

  size_t count_ = 0;
  char ssids_[kMaxProfiles][kSsidLen] = {};
  char passes_[kMaxProfiles][kPassLen] = {};
  char bssids_[kMaxProfiles][kBssidLen] = {};
  bool auto_join_[kMaxProfiles] = {};
};

}  // namespace storage
