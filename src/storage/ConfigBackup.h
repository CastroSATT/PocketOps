#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace storage {

// Store-method ZIP of LittleFS /config/*.ini (no compression — tiny text files).
class ConfigBackup {
 public:
  static constexpr size_t kMaxZipBytes = 128 * 1024;

  // Build .pobak payload (store-method archive) into out. Missing files are skipped.
  static bool buildZip(std::vector<uint8_t>& out);

  // Restore allowlisted basenames from a .pobak / store-method archive.
  // err_out optional human message.
  static bool applyZip(const uint8_t* data, size_t len, char* err_out, size_t err_len);

 private:
  ConfigBackup() = default;
};

}  // namespace storage
