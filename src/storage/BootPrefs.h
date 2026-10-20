#pragma once

#include <Arduino.h>
#include <stddef.h>

namespace storage {

// NVS preference: which tool to auto-enter after splash (empty / "menu" = stay on menu).
class BootPrefs {
 public:
  static BootPrefs& instance();

  static constexpr size_t kMaxIdLen = 24;

  void begin();  // load from NVS (after ConfigStore folders / nvs namespace ready)

  const char* toolId() const { return tool_id_; }
  // id empty, "menu", or "off" → clear (menu only). Otherwise store tool id.
  bool setToolId(const char* id);

 private:
  BootPrefs() = default;
  void persist();
  void normalize(const char* in, char* out, size_t out_len) const;

  char tool_id_[kMaxIdLen] = {};
  bool loaded_ = false;
};

}  // namespace storage
