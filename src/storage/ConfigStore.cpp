#include "storage/ConfigStore.h"

#include <FS.h>
#include <LittleFS.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "Branding.h"

namespace storage {
namespace {

constexpr const char* kFoldersPath = "/config/pocketops_folders.ini";
constexpr const char* kSettingsPath = "/config/pocketops.ini";
constexpr const char* kBtPath = "/config/pocketops_bt.ini";
constexpr const char* kMousePath = "/config/pocketops_mouse.ini";
constexpr const char* kKeyboardPath = "/config/pocketops_keyboard.ini";
constexpr const char* kUiPath = "/config/pocketops_ui.ini";
constexpr const char* kDisplayPath = "/config/pocketops_display.ini";

constexpr const char* kSeedSettingsIni =
    "; PocketOps network / wireless\n"
    "\n"
    "wifi_mode=ap\n"
    "wifi_ap_ssid=PocketOps\n"
    "wifi_ap_pass=pocketops\n"
    "wifi_ap_channel=1\n"
    "; Last joined LAN (Live Wi-Fi Connect)\n"
    "wifi_sta_ssid=\n"
    "wifi_sta_pass=\n"
    "web_port=80\n"
    "ws_port=81\n";

constexpr const char* kSeedBtIni =
    "; PocketOps Bluetooth\n"
    "; GATT / NUS UUIDs: include/Branding.h\n"
    "; Mouse step: pocketops_mouse.ini\n"
    "\n"
    "ble_device_name=PocketOps\n"
    "ble_serial_device_name=PocketOps Serial\n";

constexpr const char* kSeedMouseIni =
    "; PocketOps Mouse\n"
    "\n"
    "ble_serial_mouse_step=40\n"
    "touchpad_speed=100\n"
    "scroll_speed=100\n"
    "two_finger_scroll=1\n";

constexpr const char* kSeedKeyboardIni =
    "; PocketOps Keyboard\n"
    "\n"
    "char_interval_ms=12\n";

constexpr const char* kSeedFoldersIni =
    "; PocketOps SD card layout\n"
    "\n"
    "volume_label=PocketOps\n"
    "nvs_namespace=pocketops\n"
    "\n"
    "; /Logs is required (device logs) — always kept even if omitted\n"
    "/Logs\n";

constexpr const char* kSeedUiIni =
    "; PocketOps UI (LittleFS) — splash only\n"
    "; Version / remote / BT / BOOT button timings: include/Branding.h\n"
    "\n"
    "splash=\n"
    "splash_ms=1500\n";

constexpr const char* kSeedDisplayIni =
    "; PocketOps Display / device light (LittleFS)\n"
    "\n"
    "display_brightness=80\n"
    "display_sleep_sec=0\n"
    "light_brightness=44\n";

void trimInPlace(char* s) {
  if (!s) {
    return;
  }
  char* start = s;
  while (*start == ' ' || *start == '\t') {
    ++start;
  }
  if (start != s) {
    memmove(s, start, strlen(start) + 1);
  }
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
    s[--n] = '\0';
  }
}

void copyKey(char* dst, size_t dst_len, const char* src) {
  strncpy(dst, src, dst_len - 1);
  dst[dst_len - 1] = '\0';
}

bool normalizePath(const char* in, char* out, size_t out_len) {
  if (!in || !out || out_len < 2) {
    return false;
  }

  if (static_cast<unsigned char>(in[0]) == 0xEF &&
      static_cast<unsigned char>(in[1]) == 0xBB &&
      static_cast<unsigned char>(in[2]) == 0xBF) {
    in += 3;
  }

  while (*in == ' ' || *in == '\t') {
    ++in;
  }
  if (*in == '\0' || *in == ';' || *in == '#') {
    return false;
  }
  if (*in == '[') {
    return false;
  }

  char tmp[ConfigStore::kMaxPathLen];
  size_t n = 0;
  for (const char* p = in; *p && *p != '\r' && *p != '\n'; ++p) {
    char c = *p;
    if (c == '\\') {
      c = '/';
    }
    if (n + 1 >= sizeof(tmp)) {
      return false;
    }
    tmp[n++] = c;
  }
  while (n > 0 && (tmp[n - 1] == ' ' || tmp[n - 1] == '\t' || tmp[n - 1] == '/')) {
    --n;
  }
  tmp[n] = '\0';

  size_t start = 0;
  while (tmp[start] == '/') {
    ++start;
  }
  if (tmp[start] == '\0') {
    return false;
  }
  if (strstr(tmp + start, "..") != nullptr) {
    return false;
  }

  out[0] = '/';
  size_t o = 1;
  for (size_t i = start; tmp[i]; ++i) {
    const char c = tmp[i];
    if (c == '/' && out[o - 1] == '/') {
      continue;
    }
    if (o + 1 >= out_len) {
      return false;
    }
    out[o++] = c;
  }
  out[o] = '\0';

  size_t depth = 0;
  for (size_t i = 0; out[i]; ++i) {
    if (out[i] == '/') {
      ++depth;
    }
  }
  return depth >= 1 && depth <= 8;
}

void forEachLine(File& f, ConfigStore* self, void (ConfigStore::*fn)(char*)) {
  char line[192];
  size_t line_len = 0;

  while (f.available()) {
    const int c = f.read();
    if (c < 0) {
      break;
    }
    if (c == '\n') {
      line[line_len] = '\0';
      line_len = 0;
      (self->*fn)(line);
      continue;
    }
    if (c == '\r') {
      continue;
    }
    if (line_len + 1 < sizeof(line)) {
      line[line_len++] = static_cast<char>(c);
    }
  }
  if (line_len > 0) {
    line[line_len] = '\0';
    (self->*fn)(line);
  }
}

bool splitKeyValue(char* line, char** key, char** value) {
  trimInPlace(line);
  if (line[0] == '\0' || line[0] == ';' || line[0] == '#' || line[0] == '[') {
    return false;
  }
  char* eq = strchr(line, '=');
  if (!eq) {
    return false;
  }
  *eq = '\0';
  *key = line;
  *value = eq + 1;
  trimInPlace(*key);
  trimInPlace(*value);
  return (*key)[0] != '\0';
}

}  // namespace

ConfigStore& ConfigStore::instance() {
  static ConfigStore s;
  return s;
}

bool ConfigStore::ensureConfigDir() {
  if (LittleFS.exists("/config")) {
    File f = LittleFS.open("/config");
    const bool is_dir = f && f.isDirectory();
    if (f) {
      f.close();
    }
    if (is_dir) {
      return true;
    }
    LittleFS.remove("/config");
  }
  if (!LittleFS.mkdir("/config")) {
    Serial.println("CFG: mkdir /config failed");
    return false;
  }
  return true;
}

bool ConfigStore::seedFileIfMissing(const char* path, const char* content) {
  if (!ready_ || !path || !content) {
    return false;
  }
  if (LittleFS.exists(path)) {
    return true;
  }
  if (!ensureConfigDir()) {
    return false;
  }
  File f = LittleFS.open(path, "w");
  if (!f) {
    Serial.printf("CFG: seed create fail %s\n", path);
    return false;
  }
  f.print(content);
  f.close();
  Serial.printf("CFG: seeded %s\n", path);
  return true;
}

void ConfigStore::clearSettings() {
  wifi_mode_[0] = '\0';
  wifi_ap_ssid_[0] = '\0';
  wifi_ap_pass_[0] = '\0';
  wifi_ap_channel_ = 0;
  wifi_sta_ssid_[0] = '\0';
  wifi_sta_pass_[0] = '\0';
  web_port_ = 0;
  ws_port_ = 0;
}

void ConfigStore::clearBluetooth() {
  ble_device_name_[0] = '\0';
  ble_serial_device_name_[0] = '\0';
}

void ConfigStore::clearMouse() {
  ble_serial_mouse_step_ = 0;
  touchpad_speed_ = 0;
  scroll_speed_ = 0;
  two_finger_scroll_ = false;
  two_finger_scroll_set_ = false;
}

void ConfigStore::clearKeyboard() {
  key_char_interval_ms_ = 0xFFFF;  // unset sentinel (0 ms is a valid user value)
}

void ConfigStore::clearFolders() {
  volume_label_[0] = '\0';
  nvs_namespace_[0] = '\0';
  folder_count_ = 0;
}

void ConfigStore::clearUi() {
  splash_[0] = '\0';
  splash_ms_ = 0;
}

void ConfigStore::clearDisplay() {
  display_brightness_ = 0;
  light_brightness_ = 0;
  display_sleep_sec_ = 0;
}

bool ConfigStore::begin() {
  if (ready_) {
    return true;
  }
  if (!LittleFS.begin(true)) {
    Serial.println("CFG: LittleFS mount fail");
    ready_ = false;
    clearSettings();
    clearBluetooth();
    clearMouse();
    clearKeyboard();
    clearFolders();
    clearUi();
    clearDisplay();
    return false;
  }
  ready_ = true;
  Serial.println("CFG: LittleFS ready");
  ensureConfigDir();
  return true;
}

void ConfigStore::applyNetSetting(const char* key, const char* value) {
  if (strcmp(key, "wifi_mode") == 0) {
    if (strcmp(value, "ap") == 0 || strcmp(value, "sta") == 0 || strcmp(value, "apsta") == 0) {
      copyKey(wifi_mode_, sizeof(wifi_mode_), value);
    }
  } else if (strcmp(key, "wifi_ap_ssid") == 0) {
    copyKey(wifi_ap_ssid_, sizeof(wifi_ap_ssid_), value);
  } else if (strcmp(key, "wifi_ap_pass") == 0) {
    copyKey(wifi_ap_pass_, sizeof(wifi_ap_pass_), value);
  } else if (strcmp(key, "wifi_ap_channel") == 0) {
    const int v = atoi(value);
    if (v >= 1 && v <= 13) {
      wifi_ap_channel_ = v;
    }
  } else if (strcmp(key, "wifi_sta_ssid") == 0) {
    copyKey(wifi_sta_ssid_, sizeof(wifi_sta_ssid_), value);
  } else if (strcmp(key, "wifi_sta_pass") == 0) {
    copyKey(wifi_sta_pass_, sizeof(wifi_sta_pass_), value);
  } else if (strcmp(key, "web_port") == 0) {
    const int v = atoi(value);
    if (v > 0 && v < 65536) {
      web_port_ = v;
    }
  } else if (strcmp(key, "ws_port") == 0) {
    const int v = atoi(value);
    if (v > 0 && v < 65536) {
      ws_port_ = v;
    }
  } else if (strcmp(key, "ble_serial_mouse_step") == 0 ||
             strcmp(key, "ble_device_name") == 0 ||
             strcmp(key, "ble_serial_device_name") == 0 ||
             strcmp(key, "ble_service_uuid") == 0 ||
             strcmp(key, "ble_char_uuid") == 0 ||
             strcmp(key, "ble_nus_service_uuid") == 0 ||
             strcmp(key, "ble_nus_rx_uuid") == 0 ||
             strcmp(key, "ble_nus_tx_uuid") == 0) {
    // Legacy network keys — BT lives in pocketops_bt.ini / Branding.h
  }
}

void ConfigStore::applyBtSetting(const char* key, const char* value) {
  if (strcmp(key, "ble_device_name") == 0) {
    copyKey(ble_device_name_, sizeof(ble_device_name_), value);
  } else if (strcmp(key, "ble_serial_device_name") == 0) {
    copyKey(ble_serial_device_name_, sizeof(ble_serial_device_name_), value);
  } else if (strcmp(key, "ble_serial_mouse_step") == 0 ||
             strcmp(key, "ble_service_uuid") == 0 ||
             strcmp(key, "ble_char_uuid") == 0 ||
             strcmp(key, "ble_nus_service_uuid") == 0 ||
             strcmp(key, "ble_nus_rx_uuid") == 0 ||
             strcmp(key, "ble_nus_tx_uuid") == 0) {
    // Mouse step → pocketops_mouse.ini; UUIDs → Branding.h
  }
}

void ConfigStore::applyMouseSetting(const char* key, const char* value) {
  if (strcmp(key, "ble_serial_mouse_step") == 0) {
    const int v = atoi(value);
    if (v >= 1 && v <= 127) {
      ble_serial_mouse_step_ = static_cast<int8_t>(v);
    }
  } else if (strcmp(key, "touchpad_speed") == 0) {
    const int v = atoi(value);
    if (v >= 25 && v <= 300) {
      touchpad_speed_ = static_cast<uint16_t>(v);
    }
  } else if (strcmp(key, "scroll_speed") == 0) {
    const int v = atoi(value);
    if (v >= 25 && v <= 300) {
      scroll_speed_ = static_cast<uint16_t>(v);
    }
  } else if (strcmp(key, "two_finger_scroll") == 0) {
    two_finger_scroll_set_ = true;
    two_finger_scroll_ =
        !(value[0] == '0' || value[0] == 'n' || value[0] == 'N' || value[0] == 'f' ||
          value[0] == 'F');
  }
}

void ConfigStore::applyKeyboardSetting(const char* key, const char* value) {
  if (strcmp(key, "char_interval_ms") == 0) {
    const int v = atoi(value);
    if (v >= 0 && v <= 100) {
      key_char_interval_ms_ = static_cast<uint16_t>(v);
    }
  }
}

void ConfigStore::applyFolderSetting(const char* key, const char* value) {
  if (strcmp(key, "volume_label") == 0) {
    copyKey(volume_label_, sizeof(volume_label_), value);
  } else if (strcmp(key, "nvs_namespace") == 0) {
    copyKey(nvs_namespace_, sizeof(nvs_namespace_), value);
  }
}

void ConfigStore::applyUiSetting(const char* key, const char* value) {
  if (strcmp(key, "splash") == 0 || strcmp(key, "product_name") == 0) {
    // product_name kept as legacy alias for splash
    copyKey(splash_, sizeof(splash_), value);
  } else if (strcmp(key, "splash_ms") == 0) {
    const long v = atol(value);
    if (v > 0 && v <= 60000) {
      splash_ms_ = static_cast<uint32_t>(v);
    }
  } else if (strcmp(key, "btn_debounce_ms") == 0 ||
             strcmp(key, "btn_multi_gap_ms") == 0 ||
             strcmp(key, "btn_long_ms") == 0 ||
             strcmp(key, "firmware_version") == 0 ||
             strcmp(key, "remote_page_title") == 0 ||
             strcmp(key, "ble_serial_banner") == 0) {
    // Legacy keys ignored — baked in include/Branding.h
  }
}

void ConfigStore::applyDisplaySetting(const char* key, const char* value) {
  if (strcmp(key, "display_brightness") == 0) {
    const int v = atoi(value);
    if (v >= 5 && v <= 100) {
      display_brightness_ = static_cast<uint8_t>(v);
    }
  } else if (strcmp(key, "light_brightness") == 0) {
    const int v = atoi(value);
    if (v >= 5 && v <= 100) {
      light_brightness_ = static_cast<uint8_t>(v);
    }
  } else if (strcmp(key, "display_sleep_sec") == 0) {
    const long v = atol(value);
    if (v >= 0 && v <= 86400) {
      display_sleep_sec_ = static_cast<uint32_t>(v);
    }
  }
}

void ConfigStore::parseSettingsLine(char* line) {
  char* key = nullptr;
  char* value = nullptr;
  if (!splitKeyValue(line, &key, &value)) {
    return;
  }
  applyNetSetting(key, value);
}

void ConfigStore::parseBtLine(char* line) {
  char* key = nullptr;
  char* value = nullptr;
  if (!splitKeyValue(line, &key, &value)) {
    return;
  }
  applyBtSetting(key, value);
}

void ConfigStore::parseMouseLine(char* line) {
  char* key = nullptr;
  char* value = nullptr;
  if (!splitKeyValue(line, &key, &value)) {
    return;
  }
  applyMouseSetting(key, value);
}

void ConfigStore::parseKeyboardLine(char* line) {
  char* key = nullptr;
  char* value = nullptr;
  if (!splitKeyValue(line, &key, &value)) {
    return;
  }
  applyKeyboardSetting(key, value);
}

void ConfigStore::parseFolderLine(char* line) {
  char* key = nullptr;
  char* value = nullptr;
  if (splitKeyValue(line, &key, &value)) {
    applyFolderSetting(key, value);
    return;
  }

  char path[kMaxPathLen];
  if (!normalizePath(line, path, sizeof(path))) {
    return;
  }
  for (size_t i = 0; i < folder_count_; ++i) {
    if (strcmp(folders_[i], path) == 0) {
      return;
    }
  }
  if (folder_count_ >= kMaxFolders) {
    return;
  }
  copyKey(folders_[folder_count_], kMaxPathLen, path);
  ++folder_count_;
}

void ConfigStore::parseUiLine(char* line) {
  char* key = nullptr;
  char* value = nullptr;
  if (!splitKeyValue(line, &key, &value)) {
    return;
  }
  applyUiSetting(key, value);
}

void ConfigStore::parseDisplayLine(char* line) {
  char* key = nullptr;
  char* value = nullptr;
  if (!splitKeyValue(line, &key, &value)) {
    return;
  }
  applyDisplaySetting(key, value);
}

bool ConfigStore::loadSettingsFromFile() {
  clearSettings();
  File f = LittleFS.open(kSettingsPath, "r");
  if (!f) {
    return false;
  }
  forEachLine(f, this, &ConfigStore::parseSettingsLine);
  f.close();
  if (wifi_mode_[0] == '\0') {
    copyKey(wifi_mode_, sizeof(wifi_mode_), "ap");
  }
  return wifi_ap_ssid_[0] != '\0' && web_port_ > 0 && ws_port_ > 0;
}

bool ConfigStore::loadBluetoothFromFile() {
  clearBluetooth();
  File f = LittleFS.open(kBtPath, "r");
  if (!f) {
    return false;
  }
  forEachLine(f, this, &ConfigStore::parseBtLine);
  f.close();
  // Older bt.ini may only have mouse step — fill default advertise names.
  if (ble_device_name_[0] == '\0') {
    copyKey(ble_device_name_, sizeof(ble_device_name_), "PocketOps");
  }
  if (ble_serial_device_name_[0] == '\0') {
    copyKey(ble_serial_device_name_, sizeof(ble_serial_device_name_), "PocketOps Serial");
  }
  return ble_device_name_[0] != '\0' && ble_serial_device_name_[0] != '\0';
}

bool ConfigStore::loadMouseFromFile() {
  clearMouse();
  File f = LittleFS.open(kMousePath, "r");
  if (!f) {
    return false;
  }
  forEachLine(f, this, &ConfigStore::parseMouseLine);
  f.close();
  if (touchpad_speed_ == 0) {
    touchpad_speed_ = 100;
  }
  if (scroll_speed_ == 0) {
    scroll_speed_ = 100;
  }
  if (!two_finger_scroll_set_) {
    two_finger_scroll_ = true;
  }
  return ble_serial_mouse_step_ > 0 && touchpad_speed_ >= 25 && scroll_speed_ >= 25;
}

bool ConfigStore::loadKeyboardFromFile() {
  clearKeyboard();
  File f = LittleFS.open(kKeyboardPath, "r");
  if (!f) {
    return false;
  }
  forEachLine(f, this, &ConfigStore::parseKeyboardLine);
  f.close();
  if (key_char_interval_ms_ == 0xFFFF) {
    key_char_interval_ms_ = 12;
  }
  return key_char_interval_ms_ <= 100;
}

void ConfigStore::ensureLogsFolder() {
  constexpr const char* kLogs = "/Logs";
  for (size_t i = 0; i < folder_count_; ++i) {
    if (strcasecmp(folders_[i], kLogs) == 0) {
      copyKey(folders_[i], kMaxPathLen, kLogs);
      return;
    }
  }
  if (folder_count_ >= kMaxFolders) {
    return;
  }
  // Keep required Logs first in the list.
  for (size_t i = folder_count_; i > 0; --i) {
    copyKey(folders_[i], kMaxPathLen, folders_[i - 1]);
  }
  copyKey(folders_[0], kMaxPathLen, kLogs);
  ++folder_count_;
}

bool ConfigStore::loadFoldersFromFile() {
  clearFolders();
  File f = LittleFS.open(kFoldersPath, "r");
  if (!f) {
    return false;
  }
  forEachLine(f, this, &ConfigStore::parseFolderLine);
  f.close();
  ensureLogsFolder();
  return volume_label_[0] != '\0' && nvs_namespace_[0] != '\0' && folder_count_ > 0;
}

bool ConfigStore::loadUiFromFile() {
  clearUi();
  File f = LittleFS.open(kUiPath, "r");
  if (!f) {
    return false;
  }
  forEachLine(f, this, &ConfigStore::parseUiLine);
  f.close();
  // splash may be blank (falls back to branding::kName)
  return splash_ms_ > 0;
}

bool ConfigStore::loadDisplayFromFile() {
  clearDisplay();
  File f = LittleFS.open(kDisplayPath, "r");
  if (!f) {
    return false;
  }
  forEachLine(f, this, &ConfigStore::parseDisplayLine);
  f.close();
  return display_brightness_ >= 5 && light_brightness_ >= 5;
}

bool ConfigStore::loadSettings() {
  if (!ready_) {
    Serial.println("CFG: settings unavailable (no LittleFS)");
    clearSettings();
    return false;
  }

  seedFileIfMissing(kSettingsPath, kSeedSettingsIni);
  if (loadSettingsFromFile()) {
    Serial.printf("CFG: net from %s (ssid=%s)\n", kSettingsPath, wifi_ap_ssid_);
    return true;
  }

  Serial.printf("CFG: bad %s — re-seeding\n", kSettingsPath);
  LittleFS.remove(kSettingsPath);
  seedFileIfMissing(kSettingsPath, kSeedSettingsIni);
  if (loadSettingsFromFile()) {
    return true;
  }
  clearSettings();
  return false;
}

bool ConfigStore::loadBluetooth() {
  if (!ready_) {
    Serial.println("CFG: bluetooth unavailable (no LittleFS)");
    clearBluetooth();
    return false;
  }

  seedFileIfMissing(kBtPath, kSeedBtIni);
  if (loadBluetoothFromFile()) {
    Serial.printf("CFG: bt from %s (remote=%s serial=%s)\n", kBtPath, ble_device_name_,
                  ble_serial_device_name_);
    return true;
  }

  Serial.printf("CFG: bad %s — re-seeding\n", kBtPath);
  LittleFS.remove(kBtPath);
  seedFileIfMissing(kBtPath, kSeedBtIni);
  if (loadBluetoothFromFile()) {
    return true;
  }
  clearBluetooth();
  return false;
}

bool ConfigStore::loadMouse() {
  if (!ready_) {
    Serial.println("CFG: mouse unavailable (no LittleFS)");
    clearMouse();
    return false;
  }

  seedFileIfMissing(kMousePath, kSeedMouseIni);
  if (loadMouseFromFile()) {
    Serial.printf("CFG: mouse from %s (step=%d speed=%u scroll_spd=%u scroll=%d)\n", kMousePath,
                  static_cast<int>(ble_serial_mouse_step_),
                  static_cast<unsigned>(touchpad_speed_),
                  static_cast<unsigned>(scroll_speed_), two_finger_scroll_ ? 1 : 0);
    return true;
  }

  Serial.printf("CFG: bad %s — re-seeding\n", kMousePath);
  LittleFS.remove(kMousePath);
  seedFileIfMissing(kMousePath, kSeedMouseIni);
  if (loadMouseFromFile()) {
    return true;
  }
  clearMouse();
  return false;
}

bool ConfigStore::loadKeyboard() {
  if (!ready_) {
    Serial.println("CFG: keyboard unavailable (no LittleFS)");
    clearKeyboard();
    key_char_interval_ms_ = 12;
    return false;
  }

  seedFileIfMissing(kKeyboardPath, kSeedKeyboardIni);
  if (loadKeyboardFromFile()) {
    Serial.printf("CFG: keyboard from %s (char_interval_ms=%u)\n", kKeyboardPath,
                  static_cast<unsigned>(key_char_interval_ms_));
    return true;
  }

  Serial.printf("CFG: bad %s — re-seeding\n", kKeyboardPath);
  LittleFS.remove(kKeyboardPath);
  seedFileIfMissing(kKeyboardPath, kSeedKeyboardIni);
  if (loadKeyboardFromFile()) {
    return true;
  }
  clearKeyboard();
  key_char_interval_ms_ = 12;
  return false;
}

bool ConfigStore::loadFormatFolders() {
  if (!ready_ && !begin()) {
    clearFolders();
    Serial.println("CFG: folders unavailable (no LittleFS)");
    return false;
  }

  seedFileIfMissing(kFoldersPath, kSeedFoldersIni);
  if (loadFoldersFromFile()) {
    Serial.printf("CFG: sd from %s (label=%s folders=%u)\n", kFoldersPath, volume_label_,
                  static_cast<unsigned>(folder_count_));
    return true;
  }

  Serial.printf("CFG: bad %s — re-seeding\n", kFoldersPath);
  LittleFS.remove(kFoldersPath);
  seedFileIfMissing(kFoldersPath, kSeedFoldersIni);
  if (loadFoldersFromFile()) {
    return true;
  }
  clearFolders();
  return false;
}

bool ConfigStore::loadUi() {
  if (!ready_) {
    Serial.println("CFG: ui unavailable (no LittleFS)");
    clearUi();
    return false;
  }

  seedFileIfMissing(kUiPath, kSeedUiIni);
  if (loadUiFromFile()) {
    Serial.printf("CFG: ui from %s (splash=%s)\n", kUiPath, splashText());
    return true;
  }

  Serial.printf("CFG: bad %s — re-seeding\n", kUiPath);
  LittleFS.remove(kUiPath);
  seedFileIfMissing(kUiPath, kSeedUiIni);
  if (loadUiFromFile()) {
    return true;
  }
  clearUi();
  return false;
}

bool ConfigStore::loadDisplay() {
  if (!ready_) {
    Serial.println("CFG: display unavailable (no LittleFS)");
    clearDisplay();
    display_brightness_ = 80;
    light_brightness_ = 44;
    display_sleep_sec_ = 0;
    return false;
  }

  seedFileIfMissing(kDisplayPath, kSeedDisplayIni);
  if (loadDisplayFromFile()) {
    Serial.printf("CFG: display from %s (bl=%u led=%u sleep=%us)\n", kDisplayPath,
                  static_cast<unsigned>(display_brightness_),
                  static_cast<unsigned>(light_brightness_),
                  static_cast<unsigned>(display_sleep_sec_));
    return true;
  }

  Serial.printf("CFG: bad %s — re-seeding\n", kDisplayPath);
  LittleFS.remove(kDisplayPath);
  seedFileIfMissing(kDisplayPath, kSeedDisplayIni);
  if (loadDisplayFromFile()) {
    return true;
  }
  clearDisplay();
  display_brightness_ = 80;
  light_brightness_ = 44;
  display_sleep_sec_ = 0;
  return false;
}

const char* ConfigStore::splashText() const {
  return splash_[0] ? splash_ : branding::kName;
}

const char* ConfigStore::bleServiceUuid() const {
  return branding::kBleServiceUuid;
}

const char* ConfigStore::bleCharUuid() const {
  return branding::kBleCharUuid;
}

const char* ConfigStore::bleNusServiceUuid() const {
  return branding::kBleNusServiceUuid;
}

const char* ConfigStore::bleNusRxUuid() const {
  return branding::kBleNusRxUuid;
}

const char* ConfigStore::bleNusTxUuid() const {
  return branding::kBleNusTxUuid;
}

uint32_t ConfigStore::btnDebounceMs() const {
  return branding::kBtnDebounceMs;
}

uint32_t ConfigStore::btnMultiGapMs() const {
  return branding::kBtnMultiGapMs;
}

uint32_t ConfigStore::btnLongMs() const {
  return branding::kBtnLongMs;
}

const char* ConfigStore::formatFolder(size_t index) const {
  if (index >= folder_count_) {
    return nullptr;
  }
  return folders_[index];
}

}  // namespace storage
