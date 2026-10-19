#pragma once

#include <Arduino.h>

namespace storage {

struct TfCardInfo {
  bool present = false;
  bool dirs_ok = false;
  uint8_t type = 0;  // sdcard_type_t
  uint32_t size_mb = 0;
  uint32_t total_mb = 0;
  uint32_t used_mb = 0;
  uint32_t free_mb = 0;
  char type_name[8] = {};
};

// Global TF/microSD USB mass-storage expose switch.
// When enabled, the host PC sees the card as a USB drive; status icon shows everywhere.
class TfExpose {
 public:
  static TfExpose& instance();

  bool enabled() const { return enabled_; }
  bool mounted() const { return mounted_; }
  const char* lastError() const { return last_error_; }

  // True briefly after MSC (or other) SD read/write — drives status-icon flash.
  bool activityRecent(uint32_t window_ms = 180) const;
  void noteActivity();

  // Cached presence only — never touches the card. Call refreshCardPresent() to update.
  bool cardPresent() const { return probe_ok_; }

  // Probe the slot once (boot / enter TF tool). Safe to call when expose is off.
  bool refreshCardPresent();

  // Mount, fill size/type/dir info. Leaves mount as it found it (except errors).
  bool checkCard(TfCardInfo& out);

  // FAT format using LittleFS pocketops_folders.ini (volume label, USB names,
  // nested folders). Turns expose off during the wipe. Optional progress
  // (0–100, status) for UI.
  using FormatProgress = void (*)(int percent, const char* status);
  bool formatPocketLayout(TfCardInfo& out, FormatProgress progress = nullptr);

  // Load saved on/off from flash and apply (call once at boot).
  void loadSaved();

  // Turn USB expose on (mounts card + MSC). Returns false on failure.
  bool enable();
  // Turn expose off (hides media / stops MSC).
  void disable();
  bool toggle();

 private:
  TfExpose() = default;

  bool mountCard();
  void unmountCard();
  bool startMsc();
  void stopMsc();
  void persist(bool on);
  void fillInfo(TfCardInfo& out);
  bool ensureDirs();

  bool enabled_ = false;
  bool mounted_ = false;
  bool msc_started_ = false;
  const char* last_error_ = "";
  bool probe_ok_ = false;
  bool probed_ = false;
  volatile uint32_t activity_ms_ = 0;
};

}  // namespace storage
