#pragma once

namespace storage {

// USB DFU preference (NVS). Default OFF — TinyUSB DFU interface is only
// armed on the next boot when the switch is on.
class FirmwareUpdate {
 public:
  static FirmwareUpdate& instance();

  void begin();  // sync RAM from NVS after ConfigStore
  bool dfuEnabled() const { return dfu_on_; }
  /** True if USB.enableDFU() ran this boot (preference was on at pre-arm). */
  bool dfuArmed() const { return armed_; }
  void setDfuEnabled(bool on);

  /** Called from constructor pre-arm (before USB.begin). */
  static void noteArmed(bool armed);

 private:
  FirmwareUpdate() = default;
  void load();
  void persist();

  bool loaded_ = false;
  bool dfu_on_ = false;  // default off
  bool armed_ = false;
};

}  // namespace storage
