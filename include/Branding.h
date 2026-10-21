#pragma once

#include <stdint.h>

// Compile-time product strings, BLE IDs, BOOT timings — not editable on-device
// (same idea as config/board.ini). Change here, then rebuild + upload firmware.
namespace branding {

constexpr const char* kName = "PocketOps";
constexpr const char* kFirmwareVersion = "0.2.2";
constexpr const char* kRemotePageTitle = "PocketOps Remote";
// BLE advertise names are editable on-device (Config → Bluetooth / pocketops_bt.ini).
constexpr const char* kBleSerialBanner = "PocketOps Serial";

// BT Remote HID GATT
constexpr const char* kBleServiceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kBleCharUuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

// BT Serial HID (Nordic UART Service)
constexpr const char* kBleNusServiceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kBleNusRxUuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kBleNusTxUuid = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

// BOOT button (ms)
constexpr uint32_t kBtnDebounceMs = 30;
constexpr uint32_t kBtnMultiGapMs = 380;
constexpr uint32_t kBtnLongMs = 700;

}  // namespace branding
