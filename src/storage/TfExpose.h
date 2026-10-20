#pragma once

#include <Arduino.h>
#include <FS.h>

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

  // Web Files panel open (heartbeat). Drives orange SD badge on TFT / icon API.
  void noteFilesUi(bool open);
  bool filesUiOpen(uint32_t window_ms = 3000) const;

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

  // Load "start Expose on boot" from NVS and apply once at boot.
  void loadSaved();

  // Saved preference only — does not change live Expose on/off.
  bool bootEnabled() const { return boot_enabled_; }
  void setBootEnabled(bool on);

  // Turn USB expose on (mounts card + MSC). Returns false on failure.
  // Does not change the boot preference.
  bool enable();
  // Turn expose off — same host-release path Format uses (wait, then unmount).
  // Does not change the boot preference.
  void disable();
  // Files browser: same path, but refuses / aborts if Mac is still busy.
  bool disableSafe();
  bool toggle();

  // Call from App::loop — finishes host eject (Mac Finder eject) off the USB callback.
  void tick();
  // Called from MSC start/stop callback when the host ejects the volume.
  void noteHostEject();

  // Mount (briefly disabling USB expose if needed), write UTF-8 text, restore.
  // path is absolute on the card, e.g. "/Logs/Lan/foo.csv".
  bool writeTextFile(const char* path, const char* data);

  // Exclusive card access for web file browser.
  // Soft-pauses USB MSC if Expose is on (does not rewrite NVS). Safe teardown
  // waits for the host — never hard-cuts the bus mid-transfer.
  bool beginExclusive();
  void endExclusive();
  bool exclusiveHeld() const { return exclusive_; }

  // Paths must be absolute ("/…"); ".." rejected. Card must be in exclusive mode
  // (or already mounted with expose off).
  bool listDirectory(const char* path, String& json_array_out);
  bool makeDir(const char* path);
  bool removePath(const char* path);  // file or directory tree
  bool pathExists(const char* path) const;
  bool isDirectory(const char* path) const;
  // Open for read/write while exclusive; caller must close before endExclusive().
  File openFile(const char* path, const char* mode);

 private:
  TfExpose() = default;

  bool mountCard();
  void unmountCard();
  bool startMsc();
  void stopMsc();
  void persistBoot();
  void fillInfo(TfCardInfo& out);
  bool ensureDirs();
  static bool sanitizePath(const char* path, char* out, size_t out_len);

  // Shared Expose teardown (format + safe-off + disable):
  // mediaPresent(false) → end MSC → wait for host → unmount → USB re-enum.
  // safe=true: refuse if host is busy (Files dialog). safe=false: wait it out (format).
  bool releaseMscAndUnmount(bool safe, FormatProgress progress = nullptr);

  void markMscCooloff(uint32_t ms = 1800);
  void waitMscCooloff();
  void usbReenumerateHost();

  bool enabled_ = false;
  bool boot_enabled_ = false;
  bool mounted_ = false;
  bool msc_started_ = false;
  bool exclusive_ = false;
  bool exclusive_restore_expose_ = false;
  const char* last_error_ = "";
  bool probe_ok_ = false;
  bool probed_ = false;
  volatile uint32_t activity_ms_ = 0;
  volatile uint32_t files_ui_ms_ = 0;
  // After MSC stop: wait before remounting for Files (Mac ghost volumes).
  uint32_t msc_cooloff_until_ms_ = 0;
  volatile bool host_eject_pending_ = false;
};

}  // namespace storage
