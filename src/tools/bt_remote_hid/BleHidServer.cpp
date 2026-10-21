#include "tools/bt_remote_hid/BleHidServer.h"

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <cstring>

#include "ble/BleGattBoot.h"
#include "hid/HidProtocol.h"
#include "hid/UsbHid.h"
#include "storage/ConfigStore.h"

namespace {

bt_remote_hid::BleHidServer* g_ble = nullptr;
BLEServer* g_server = nullptr;
BLECharacteristic* g_char = nullptr;

void onClientCount(uint8_t n) {
  if (g_ble) {
    g_ble->setClientCount(n);
  }
}

ble::ClientCountCallbacks g_server_cb(onClientCount);

class CharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    std::string value = characteristic->getValue();
    if (value.empty()) {
      return;
    }
    char buf[256];
    const size_t n = value.size() < sizeof(buf) - 1 ? value.size() : sizeof(buf) - 1;
    memcpy(buf, value.data(), n);
    buf[n] = '\0';
    hid::HidProtocol::processMessage(buf);
  }
};

CharCallbacks g_char_cb;

}  // namespace

namespace bt_remote_hid {

void BleHidServer::begin() {
  if (running_) {
    return;
  }

  g_ble = this;
  clients_ = 0;

  auto& cfg = storage::ConfigStore::instance();
  cfg.loadSettings();
  g_server = ble::initServer(cfg.bleDeviceName(), &g_server_cb);

  BLEService* service = g_server->createService(cfg.bleServiceUuid());
  g_char = service->createCharacteristic(
      cfg.bleCharUuid(),
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  g_char->setCallbacks(&g_char_cb);

  service->start();
  ble::startServiceAdvertising(cfg.bleServiceUuid());

  hid::UsbHid::instance().begin();
  running_ = true;
}

void BleHidServer::loop() {
  // BLE stack is callback-driven; nothing to poll.
}

void BleHidServer::end() {
  if (!running_) {
    return;
  }

  ble::stopAndDeinit();
  g_server = nullptr;
  g_char = nullptr;
  g_ble = nullptr;
  clients_ = 0;
  running_ = false;
}

}  // namespace bt_remote_hid
