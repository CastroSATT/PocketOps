#pragma once

#include <Arduino.h>
#include <FS.h>
#include <stddef.h>
#include <stdint.h>

#include <WebServer.h>

namespace storage {

// On-chip session logs (LittleFS /logs). Max 10 files, FIFO.
// Always available without SD. Optionally mirrored to SD /Logs/Device when
// the card is free (Expose off).
class DeviceLog {
 public:
  static DeviceLog& instance();

  static constexpr size_t kMaxFiles = 10;
  static constexpr size_t kMaxFileBytes = 48 * 1024;
  static constexpr size_t kMaxNameLen = 40;
  static constexpr size_t kMaxLineLen = 200;

  enum class Level : uint8_t { Info = 0, Warn = 1, Error = 2 };

  // Call after LittleFS is mounted. Starts a new session file if logging is on.
  void begin();

  // Persist on/off (NVS). Off stops writing new lines; old files stay readable.
  bool enabled() const { return enabled_; }
  void setEnabled(bool on);

  void info(const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
  void warn(const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
  void error(const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
  void log(Level level, const char* tag, const char* fmt, ...)
      __attribute__((format(printf, 4, 5)));
  void vlog(Level level, const char* tag, const char* fmt, va_list ap);

  const char* currentName() const { return current_; }
  size_t count() const { return count_; }
  const char* nameAt(size_t i) const;  // oldest → newest
  size_t sizeAt(size_t i) const;

  bool removeAt(size_t index);
  bool removeNamed(const char* name);
  bool clearAll();

  // Open LittleFS file for streaming (caller closes). nullptr if bad name.
  File openRead(const char* name);

 private:
  DeviceLog() = default;

  bool ensureDir();
  bool loadIndex();
  bool saveIndex();
  void startNewSession();
  void stopSession();
  void evictOldest();
  void appendLine(const char* line);
  void maybeRotateBySize();
  void mirrorToSd(const char* name);
  bool validName(const char* name) const;
  void pathFor(const char* name, char* out, size_t out_len) const;
  void persistEnabled();
  void loadEnabled();

  size_t count_ = 0;
  char names_[kMaxFiles][kMaxNameLen] = {};
  char current_[kMaxNameLen] = {};
  size_t current_bytes_ = 0;
  bool ready_ = false;     // LittleFS /logs usable
  bool enabled_ = false;   // write new lines (default off — enable in Config → Logs)
};

void registerDeviceLogRoutes(WebServer& http);

}  // namespace storage

// Shorthand — safe after DeviceLog::begin().
#define PO_LOGI(tag, ...) ::storage::DeviceLog::instance().info((tag), __VA_ARGS__)
#define PO_LOGW(tag, ...) ::storage::DeviceLog::instance().warn((tag), __VA_ARGS__)
#define PO_LOGE(tag, ...) ::storage::DeviceLog::instance().error((tag), __VA_ARGS__)
