#pragma once

#include <BLEServer.h>
#include <stdint.h>

namespace ble {

using ClientCountFn = void (*)(uint8_t count);

/** Shared connect/disconnect: client count + re-advertise while stack is up. */
class ClientCountCallbacks : public BLEServerCallbacks {
 public:
  explicit ClientCountCallbacks(ClientCountFn fn);
  void onConnect(BLEServer* server) override;
  void onDisconnect(BLEServer* server) override;

 private:
  ClientCountFn fn_;
};

bool advertisingActive();
void setAdvertisingActive(bool on);

/** BLEDevice::init + createServer + optional callbacks. */
BLEServer* initServer(const char* device_name, BLEServerCallbacks* callbacks);

/** Standard SoftAP-style advertise for one primary service UUID. */
void startServiceAdvertising(const char* service_uuid);

/** Stop advertising and deinit the BLE stack. */
void stopAndDeinit();

}  // namespace ble
