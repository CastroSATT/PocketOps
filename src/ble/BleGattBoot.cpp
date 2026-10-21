#include "ble/BleGattBoot.h"

#include <BLEDevice.h>
#include <BLEUtils.h>

namespace ble {
namespace {

bool g_adv = false;

}  // namespace

ClientCountCallbacks::ClientCountCallbacks(ClientCountFn fn) : fn_(fn) {}

void ClientCountCallbacks::onConnect(BLEServer* /*server*/) {
  if (fn_) {
    fn_(1);
  }
}

void ClientCountCallbacks::onDisconnect(BLEServer* /*server*/) {
  if (fn_) {
    fn_(0);
  }
  if (g_adv) {
    BLEDevice::startAdvertising();
  }
}

bool advertisingActive() { return g_adv; }

void setAdvertisingActive(bool on) { g_adv = on; }

BLEServer* initServer(const char* device_name, BLEServerCallbacks* callbacks) {
  BLEDevice::init(device_name && device_name[0] ? device_name : "PocketOps");
  BLEServer* server = BLEDevice::createServer();
  if (server && callbacks) {
    server->setCallbacks(callbacks);
  }
  return server;
}

void startServiceAdvertising(const char* service_uuid) {
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  if (!advertising) {
    return;
  }
  if (service_uuid && service_uuid[0]) {
    advertising->addServiceUUID(service_uuid);
  }
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  g_adv = true;
}

void stopAndDeinit() {
  g_adv = false;
  if (BLEDevice::getAdvertising()) {
    BLEDevice::getAdvertising()->stop();
  }
  BLEDevice::deinit(true);
}

}  // namespace ble
