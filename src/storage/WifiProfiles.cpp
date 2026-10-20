#include "storage/WifiProfiles.h"

#include <FS.h>
#include <LittleFS.h>
#include <string.h>

#include "storage/ConfigStore.h"
#include "storage/UsbConsole.h"
#include "util/TextUtil.h"

namespace storage {
namespace {

constexpr const char* kPath = "/config/wifi_profiles.ini";
constexpr const char* kSeed =
    "; PocketOps Wi-Fi profiles (auto-join, up to 100)\n"
    "; One network = ssid= / pass= / bssid= / auto_join=0|1. Blank line between OK.\n"
    "; SoftAP stays available if none of these are in range.\n";

using util::copyKey;
using util::trimInPlace;

bool parseBool01(const char* value, bool default_val) {
  if (!value || !value[0]) {
    return default_val;
  }
  if (value[0] == '0' || value[0] == 'n' || value[0] == 'N' || value[0] == 'f' ||
      value[0] == 'F') {
    return false;
  }
  if (value[0] == '1' || value[0] == 'y' || value[0] == 'Y' || value[0] == 't' ||
      value[0] == 'T') {
    return true;
  }
  return default_val;
}

}  // namespace

WifiProfiles& WifiProfiles::instance() {
  static WifiProfiles s;
  return s;
}

void WifiProfiles::clear() {
  count_ = 0;
  for (size_t i = 0; i < kMaxProfiles; ++i) {
    ssids_[i][0] = '\0';
    passes_[i][0] = '\0';
    bssids_[i][0] = '\0';
    auto_join_[i] = true;
  }
}

int WifiProfiles::indexOf(const char* ssid) const {
  if (!ssid || !ssid[0]) {
    return -1;
  }
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(ssids_[i], ssid) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool WifiProfiles::ensureFile() {
  auto& cfg = ConfigStore::instance();
  if (!cfg.ready() && !cfg.begin()) {
    return false;
  }
  if (!LittleFS.exists("/config") && !LittleFS.mkdir("/config")) {
    return false;
  }
  if (LittleFS.exists(kPath)) {
    return true;
  }
  File f = LittleFS.open(kPath, "w");
  if (!f) {
    return false;
  }
  f.print(kSeed);
  f.close();
  PoSer().printf("WIFI: seeded %s\n", kPath);
  return true;
}

bool WifiProfiles::load() {
  clear();
  if (!ensureFile()) {
    return false;
  }
  File f = LittleFS.open(kPath, "r");
  if (!f) {
    return false;
  }

  char line[160];
  char pending_ssid[kSsidLen] = {};
  char pending_pass[kPassLen] = {};
  char pending_bssid[kBssidLen] = {};
  bool pending_auto = true;
  bool have_ssid = false;
  bool have_pass = false;
  bool have_auto = false;

  auto commit = [&]() {
    if (!have_ssid || !pending_ssid[0] || count_ >= kMaxProfiles) {
      return;
    }
    for (size_t i = 0; i < count_; ++i) {
      if (strcmp(ssids_[i], pending_ssid) == 0) {
        if (have_pass) {
          copyKey(passes_[i], kPassLen, pending_pass);
        }
        if (pending_bssid[0]) {
          copyKey(bssids_[i], kBssidLen, pending_bssid);
        }
        if (have_auto) {
          auto_join_[i] = pending_auto;
        }
        return;
      }
    }
    copyKey(ssids_[count_], kSsidLen, pending_ssid);
    copyKey(passes_[count_], kPassLen, have_pass ? pending_pass : "");
    copyKey(bssids_[count_], kBssidLen, pending_bssid);
    auto_join_[count_] = have_auto ? pending_auto : true;
    ++count_;
  };

  auto resetPending = [&]() {
    pending_ssid[0] = '\0';
    pending_pass[0] = '\0';
    pending_bssid[0] = '\0';
    pending_auto = true;
    have_ssid = false;
    have_pass = false;
    have_auto = false;
  };

  while (f.available()) {
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    trimInPlace(line);
    if (!line[0] || line[0] == ';' || line[0] == '#') {
      continue;
    }
    char* eq = strchr(line, '=');
    if (!eq) {
      continue;
    }
    *eq = '\0';
    char* key = line;
    char* value = eq + 1;
    trimInPlace(key);
    trimInPlace(value);
    if (strcmp(key, "ssid") == 0) {
      if (have_ssid && pending_ssid[0]) {
        commit();
      }
      resetPending();
      copyKey(pending_ssid, sizeof(pending_ssid), value);
      have_ssid = pending_ssid[0] != '\0';
    } else if (strcmp(key, "pass") == 0 && have_ssid) {
      copyKey(pending_pass, sizeof(pending_pass), value);
      have_pass = true;
    } else if (strcmp(key, "bssid") == 0 && have_ssid) {
      copyKey(pending_bssid, sizeof(pending_bssid), value);
    } else if ((strcmp(key, "auto_join") == 0 || strcmp(key, "auto") == 0) && have_ssid) {
      pending_auto = parseBool01(value, true);
      have_auto = true;
    }
  }
  if (have_ssid && pending_ssid[0]) {
    commit();
  }
  f.close();
  PoSer().printf("WIFI: profiles loaded (%u)\n", static_cast<unsigned>(count_));
  return true;
}

bool WifiProfiles::save() {
  if (!ensureFile()) {
    return false;
  }
  File f = LittleFS.open(kPath, "w");
  if (!f) {
    return false;
  }
  f.print(kSeed);
  f.print("\n");
  for (size_t i = 0; i < count_; ++i) {
    f.printf("ssid=%s\n", ssids_[i]);
    f.printf("pass=%s\n", passes_[i]);
    if (bssids_[i][0]) {
      f.printf("bssid=%s\n", bssids_[i]);
    }
    f.printf("auto_join=%d\n\n", auto_join_[i] ? 1 : 0);
  }
  f.close();
  return true;
}

bool WifiProfiles::clearAll() {
  clear();
  if (LittleFS.exists(kPath)) {
    LittleFS.remove(kPath);
  }
  const bool ok = save();
  PoSer().printf("WIFI: profiles cleared (%s)\n", ok ? "ok" : "fail");
  return ok;
}

const char* WifiProfiles::ssid(size_t index) const {
  if (index >= count_) {
    return "";
  }
  return ssids_[index];
}

const char* WifiProfiles::pass(size_t index) const {
  if (index >= count_) {
    return "";
  }
  return passes_[index];
}

const char* WifiProfiles::bssid(size_t index) const {
  if (index >= count_) {
    return "";
  }
  return bssids_[index];
}

bool WifiProfiles::autoJoin(size_t index) const {
  if (index >= count_) {
    return false;
  }
  return auto_join_[index];
}

bool WifiProfiles::findPass(const char* ssid, char* pass_out, size_t pass_len) const {
  const int i = indexOf(ssid);
  if (i < 0 || !pass_out || pass_len == 0) {
    return false;
  }
  copyKey(pass_out, pass_len, passes_[i]);
  return true;
}

bool WifiProfiles::hasSsid(const char* ssid) const {
  return indexOf(ssid) >= 0;
}

bool WifiProfiles::isAutoJoin(const char* ssid) const {
  const int i = indexOf(ssid);
  return i >= 0 && auto_join_[i];
}

bool WifiProfiles::upsert(const char* ssid, const char* pass, const char* bssid,
                          bool auto_join_set, bool auto_join) {
  if (!ssid || !ssid[0]) {
    return false;
  }
  const int existing = indexOf(ssid);
  if (existing >= 0) {
    const size_t i = static_cast<size_t>(existing);
    copyKey(passes_[i], kPassLen, pass ? pass : "");
    if (bssid && bssid[0]) {
      copyKey(bssids_[i], kBssidLen, bssid);
    }
    if (auto_join_set) {
      auto_join_[i] = auto_join;
    }
    return save();
  }
  if (count_ >= kMaxProfiles) {
    PoSer().println("WIFI: profile list full");
    return false;
  }
  copyKey(ssids_[count_], kSsidLen, ssid);
  copyKey(passes_[count_], kPassLen, pass ? pass : "");
  copyKey(bssids_[count_], kBssidLen, bssid ? bssid : "");
  auto_join_[count_] = auto_join_set ? auto_join : true;
  ++count_;
  return save();
}

bool WifiProfiles::setAutoJoin(const char* ssid, bool on) {
  const int i = indexOf(ssid);
  if (i < 0) {
    return false;
  }
  auto_join_[i] = on;
  return save();
}

bool WifiProfiles::remove(const char* ssid) {
  const int idx = indexOf(ssid);
  if (idx < 0) {
    return false;
  }
  const size_t i = static_cast<size_t>(idx);
  for (size_t j = i + 1; j < count_; ++j) {
    copyKey(ssids_[j - 1], kSsidLen, ssids_[j]);
    copyKey(passes_[j - 1], kPassLen, passes_[j]);
    copyKey(bssids_[j - 1], kBssidLen, bssids_[j]);
    auto_join_[j - 1] = auto_join_[j];
  }
  --count_;
  ssids_[count_][0] = '\0';
  passes_[count_][0] = '\0';
  bssids_[count_][0] = '\0';
  auto_join_[count_] = true;
  return save();
}

}  // namespace storage
