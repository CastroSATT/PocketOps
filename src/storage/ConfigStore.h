#pragma once

#include <stddef.h>
#include <stdint.h>

namespace storage {

// On-chip LittleFS config (PlatformIO data/ → uploadfs).
// Runtime settings are seeded once if missing, then always read from files.
// Board GPIO/TFT are compile-time only: config/board.ini → build script.
class ConfigStore {
 public:
  static ConfigStore& instance();

  bool begin();
  bool ready() const { return ready_; }

  bool loadSettings();
  bool loadBluetooth();
  bool loadMouse();
  bool loadKeyboard();
  bool loadFormatFolders();
  bool loadUi();
  bool loadDisplay();

  // pocketops.ini — Wi-Fi / web ports
  const char* wifiMode() const { return wifi_mode_; }  // ap | sta | apsta
  const char* wifiApSsid() const { return wifi_ap_ssid_; }
  const char* wifiApPass() const { return wifi_ap_pass_; }
  int wifiApChannel() const { return wifi_ap_channel_; }
  const char* wifiStaSsid() const { return wifi_sta_ssid_; }
  const char* wifiStaPass() const { return wifi_sta_pass_; }
  int webPort() const { return web_port_; }
  int wsPort() const { return ws_port_; }

  // pocketops_bt.ini — advertise names (UUIDs in Branding.h)
  const char* bleDeviceName() const { return ble_device_name_; }
  const char* bleSerialDeviceName() const { return ble_serial_device_name_; }
  const char* bleServiceUuid() const;
  const char* bleCharUuid() const;
  const char* bleNusServiceUuid() const;
  const char* bleNusRxUuid() const;
  const char* bleNusTxUuid() const;

  // pocketops_mouse.ini
  int8_t bleSerialMouseStep() const { return ble_serial_mouse_step_; }
  uint16_t touchpadSpeed() const { return touchpad_speed_; }  // percent 25–300
  uint16_t scrollSpeed() const { return scroll_speed_; }      // percent 25–300
  bool twoFingerScroll() const { return two_finger_scroll_; }

  // pocketops_keyboard.ini
  uint16_t keyCharIntervalMs() const { return key_char_interval_ms_; }  // 0–100

  // pocketops_folders.ini
  const char* volumeLabel() const { return volume_label_; }
  const char* nvsNamespace() const { return nvs_namespace_; }
  size_t formatFolderCount() const { return folder_count_; }
  const char* formatFolder(size_t index) const;

  // pocketops_ui.ini — splash text + duration only.
  const char* splashText() const;  // blank → "PocketOps"
  const char* productName() const { return splashText(); }  // menu / splash label
  uint32_t splashMs() const { return splash_ms_; }
  uint32_t btnDebounceMs() const;
  uint32_t btnMultiGapMs() const;
  uint32_t btnLongMs() const;

  // pocketops_display.ini — TFT / LED brightness + sleep
  uint8_t displayBrightness() const { return display_brightness_; }  // 5–100
  uint8_t lightBrightness() const { return light_brightness_; }      // 5–100
  uint32_t displaySleepSec() const { return display_sleep_sec_; }    // 0 = never

  static constexpr size_t kMaxFolders = 32;
  static constexpr size_t kMaxPathLen = 80;

 private:
  ConfigStore() = default;
  void clearSettings();
  void clearBluetooth();
  void clearMouse();
  void clearKeyboard();
  void clearFolders();
  void ensureLogsFolder();  // /Logs is always required for device logging
  void clearUi();
  void clearDisplay();
  void applyNetSetting(const char* key, const char* value);
  void applyBtSetting(const char* key, const char* value);
  void applyMouseSetting(const char* key, const char* value);
  void applyKeyboardSetting(const char* key, const char* value);
  void applyFolderSetting(const char* key, const char* value);
  void applyUiSetting(const char* key, const char* value);
  void applyDisplaySetting(const char* key, const char* value);
  void parseSettingsLine(char* line);
  void parseBtLine(char* line);
  void parseMouseLine(char* line);
  void parseKeyboardLine(char* line);
  void parseFolderLine(char* line);
  void parseUiLine(char* line);
  void parseDisplayLine(char* line);
  bool ensureConfigDir();
  bool seedFileIfMissing(const char* path, const char* content);
  bool loadSettingsFromFile();
  bool loadBluetoothFromFile();
  bool loadMouseFromFile();
  bool loadKeyboardFromFile();
  bool loadFoldersFromFile();
  bool loadUiFromFile();
  bool loadDisplayFromFile();

  bool ready_ = false;

  char wifi_mode_[8] = {};
  char wifi_ap_ssid_[33] = {};
  char wifi_ap_pass_[65] = {};
  int wifi_ap_channel_ = 0;
  char wifi_sta_ssid_[33] = {};
  char wifi_sta_pass_[65] = {};
  int web_port_ = 0;
  int ws_port_ = 0;

  char ble_device_name_[32] = {};
  char ble_serial_device_name_[32] = {};
  int8_t ble_serial_mouse_step_ = 0;
  uint16_t touchpad_speed_ = 0;
  uint16_t scroll_speed_ = 0;
  bool two_finger_scroll_ = false;
  bool two_finger_scroll_set_ = false;
  uint16_t key_char_interval_ms_ = 0;

  char volume_label_[12] = {};
  char nvs_namespace_[16] = {};
  size_t folder_count_ = 0;
  char folders_[kMaxFolders][kMaxPathLen] = {};

  char splash_[32] = {};
  uint32_t splash_ms_ = 0;

  uint8_t display_brightness_ = 0;
  uint8_t light_brightness_ = 0;
  uint32_t display_sleep_sec_ = 0;
};

}  // namespace storage
