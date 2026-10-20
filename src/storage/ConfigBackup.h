#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace storage {

struct FactoryResetOpts {
  bool settings = true;       // LittleFS .ini → seed defaults
  bool nvs_prefs = true;      // boot tool, expose-on-boot, USB serial, device log
  bool wifi_profiles = true;  // saved Wi‑Fi networks
  bool lan_profiles = true;   // LAN scan profiles
  bool device_logs = true;    // finished log sessions (keeps live)
};

// Store-method ZIP of LittleFS /config/*.ini plus virtual pocketops_prefs.ini (NVS).
class ConfigBackup {
 public:
  static constexpr size_t kMaxZipBytes = 128 * 1024;
  // Must match UI typed confirm (case-sensitive).
  static constexpr const char* kFactoryConfirm = "FACTORY RESET";

  // Build .pobak payload (store-method archive) into out. Missing .ini files are skipped;
  // pocketops_prefs.ini (expose_boot, boot_tool, usb_serial, device_log) is always included.
  static bool buildZip(std::vector<uint8_t>& out);

  // Restore allowlisted basenames + pocketops_prefs.ini from a .pobak / store-method archive.
  // err_out optional human message.
  static bool applyZip(const uint8_t* data, size_t len, char* err_out, size_t err_len);

  // Wipe selected on-chip data. Caller must have verified confirm == kFactoryConfirm.
  // At least one option must be true. err_out optional.
  static bool factoryReset(const FactoryResetOpts& opts, char* err_out, size_t err_len);

 private:
  ConfigBackup() = default;
};

}  // namespace storage
