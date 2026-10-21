#pragma once

#include <stddef.h>
#include <stdint.h>

namespace storage {

struct FactoryResetOpts {
  bool settings = true;       // LittleFS .ini → seed defaults
  bool nvs_prefs = true;      // boot tool, expose-on-boot, USB serial, device log
  bool wifi_profiles = true;  // saved Wi‑Fi networks
  bool lan_profiles = true;   // LAN scan profiles
  bool device_logs = true;    // finished log sessions (keeps live)
};

// Store-method ZIP of LittleFS /config/*.ini plus virtual pocketops_prefs.ini (NVS).
// Build/restore stream through a temp file so heap peak is one entry, not the whole archive.
class ConfigBackup {
 public:
  static constexpr size_t kMaxZipBytes = 128 * 1024;
  static constexpr size_t kMaxEntryBytes = 48 * 1024;
  static constexpr const char* kTmpDownload = "/config/.pobak_dl.tmp";
  static constexpr const char* kTmpUpload = "/config/.pobak_ul.tmp";
  // Must match UI typed confirm (case-sensitive).
  static constexpr const char* kFactoryConfirm = "FACTORY RESET";

  // Build .pobak to LittleFS path (chunked). Missing .ini skipped; prefs always included.
  static bool buildZipToFile(const char* path);

  // Restore allowlisted basenames + prefs from a .pobak on LittleFS (streams entries).
  static bool applyZipFromFile(const char* path, char* err_out, size_t err_len);

  // Wipe selected on-chip data. Caller must have verified confirm == kFactoryConfirm.
  static bool factoryReset(const FactoryResetOpts& opts, char* err_out, size_t err_len);

  static void removeTemp(const char* path);

 private:
  ConfigBackup() = default;
};

}  // namespace storage
