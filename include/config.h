#pragma once

// PocketOps defaults — tweak as needed

namespace config {

// Firmware identity (shown on boot splash + Serial)
constexpr const char* FIRMWARE_VERSION = "0.1.0";
constexpr unsigned SPLASH_MS = 1500;

// Wi‑Fi SoftAP for Remote HID tool
constexpr const char* WIFI_AP_SSID = "PocketOps";
constexpr const char* WIFI_AP_PASS = "pocketops";
constexpr int WIFI_AP_CHANNEL = 1;
constexpr int WEB_PORT = 80;
constexpr int WS_PORT = 81;

// BLE for BT Remote HID (Web Bluetooth on desktop Chrome)
constexpr const char* BLE_DEVICE_NAME = "PocketOps";
constexpr const char* BLE_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* BLE_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

// Nordic UART Service for BT Serial HID (iOS / serial BLE apps)
constexpr const char* BLE_SERIAL_DEVICE_NAME = "PocketOps Serial";
constexpr const char* BLE_NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* BLE_NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";  // app → device
constexpr const char* BLE_NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";  // device → app
constexpr int8_t BLE_SERIAL_MOUSE_STEP = 40;  // WASD step in //mouse mode

// BOOT button timing (ms)
constexpr uint32_t BTN_DEBOUNCE_MS = 30;
constexpr uint32_t BTN_MULTI_GAP_MS = 380;
constexpr uint32_t BTN_LONG_MS = 700;

// Display
constexpr int SCREEN_W = 160;
constexpr int SCREEN_H = 80;

}  // namespace config
