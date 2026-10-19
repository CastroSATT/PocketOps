#include "tools/bt_serial_hid/BleSerialHidServer.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <cstring>

#include "Branding.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "hid/HidProtocol.h"
#include "hid/UsbHid.h"
#include "storage/ConfigStore.h"

namespace {

bt_serial_hid::BleSerialHidServer* g_serial = nullptr;
BLEServer* g_server = nullptr;
BLECharacteristic* g_rx = nullptr;
BLECharacteristic* g_tx = nullptr;
BLE2902* g_cccd = nullptr;
bool g_advertising = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* /*pServer*/) override {
    if (g_serial) {
      g_serial->setClientCount(1);
    }
  }

  void onDisconnect(BLEServer* /*pServer*/) override {
    if (g_serial) {
      g_serial->setClientCount(0);
    }
    if (g_advertising) {
      BLEDevice::startAdvertising();
    }
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    if (!g_serial) {
      return;
    }
    std::string value = characteristic->getValue();
    if (value.empty()) {
      return;
    }
    g_serial->feedBytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
  }
};

class CccdCallbacks : public BLEDescriptorCallbacks {
  void onWrite(BLEDescriptor* descriptor) override {
    if (!g_serial || !descriptor) {
      return;
    }
    const uint8_t* v = descriptor->getValue();
    const size_t len = descriptor->getLength();
    if (len >= 1 && (v[0] & 0x01)) {
      g_serial->onNotifyEnabled();
    }
  }
};

ServerCallbacks g_server_cb;
RxCallbacks g_rx_cb;
CccdCallbacks g_cccd_cb;

bool eqIgnoreCase(const char* a, const char* b) { return strcasecmp(a, b) == 0; }

}  // namespace

namespace bt_serial_hid {

void BleSerialHidServer::setClientCount(uint8_t n) {
  clients_ = n;
  if (n == 0) {
    welcome_sent_ = false;
    welcome_at_ms_ = 0;
    line_len_ = 0;
    collecting_cmd_ = false;
    mode_ = SerialMode::Menu;
  } else {
    welcome_at_ms_ = millis() + 400;
  }
}

void BleSerialHidServer::onNotifyEnabled() { welcome_at_ms_ = millis() + 50; }

void BleSerialHidServer::notifyLine(const char* line) {
  if (!g_tx || !line || clients_ == 0) {
    return;
  }
  g_tx->setValue(reinterpret_cast<uint8_t*>(const_cast<char*>(line)), strlen(line));
  g_tx->notify();
  delay(20);
}

void BleSerialHidServer::setMode(SerialMode mode) {
  mode_ = mode;
  line_len_ = 0;
  collecting_cmd_ = false;
  sendModeBanner();
}

void BleSerialHidServer::sendKeyCodes() {
  notifyLine("--- key codes ---\n");
  notifyLine("//enter  Enter\n");
  notifyLine("//esc    Escape\n");
  notifyLine("//tab    Tab\n");
  notifyLine("//bs     Backspace\n");
  notifyLine("//del    Delete\n");
  notifyLine("//space  Space\n");
  notifyLine("//up //down\n");
  notifyLine("//left //right\n");
  notifyLine("//vulcanmac Cmd+Opt+Esc\n");
  notifyLine("//vulcanpc Ctrl+Alt+Del\n");
  notifyLine("empty line=Enter\n");
}

void BleSerialHidServer::sendModeBanner() {
  switch (mode_) {
    case SerialMode::Key:
      notifyLine("MODE key\n");
      notifyLine("text types live\n");
      sendKeyCodes();
      notifyLine("//mouse switches\n");
      break;
    case SerialMode::Mouse:
      notifyLine("MODE mouse\n");
      notifyLine("wasd=move\n");
      notifyLine("q=L e=R click\n");
      notifyLine("//key switches\n");
      break;
    default:
      notifyLine("send //key or\n");
      notifyLine("//mouse to start\n");
      break;
  }
}

void BleSerialHidServer::sendHelp() {
  {
    char banner[64];
    snprintf(banner, sizeof(banner), "%s\n",
             branding::kBleSerialBanner);
    notifyLine(banner);
  }
  notifyLine("--- commands ---\n");
  notifyLine("//key\n");
  notifyLine(" live typing mode\n");
  notifyLine(" send text lines\n");
  sendKeyCodes();
  notifyLine("//mouse\n");
  notifyLine(" wasd = move\n");
  notifyLine(" q = left click\n");
  notifyLine(" e = right click\n");
  notifyLine("//help\n");
  notifyLine(" this list again\n");
  notifyLine("Switch anytime with\n");
  notifyLine("//key or //mouse\n");
}

void BleSerialHidServer::sendWelcome() {
  if (welcome_sent_ || clients_ == 0) {
    return;
  }
  welcome_sent_ = true;
  mode_ = SerialMode::Menu;
  sendHelp();
  notifyLine("Pick //key or\n");
  notifyLine("//mouse now.\n");
}

bool BleSerialHidServer::tryMagic(const char* line) {
  if (!line || !line[0]) {
    return false;
  }

  // Copy + trim so " //mouse " / trailing junk still match
  char cmd[32];
  size_t n = 0;
  while (*line == ' ' || *line == '\t') {
    ++line;
  }
  while (*line && n + 1 < sizeof(cmd)) {
    cmd[n++] = *line++;
  }
  cmd[n] = '\0';
  while (n > 0 && (cmd[n - 1] == ' ' || cmd[n - 1] == '\t')) {
    cmd[--n] = '\0';
  }

  if (eqIgnoreCase(cmd, "//key")) {
    setMode(SerialMode::Key);
    return true;
  }
  if (eqIgnoreCase(cmd, "//mouse")) {
    setMode(SerialMode::Mouse);
    return true;
  }
  if (eqIgnoreCase(cmd, "//help") || eqIgnoreCase(cmd, "help") || strcmp(cmd, "?") == 0) {
    sendHelp();
    return true;
  }
  if (trySpecialKey(cmd)) {
    return true;
  }
  return false;
}

bool BleSerialHidServer::trySpecialKey(const char* cmd) {
  if (!cmd || cmd[0] != '/') {
    return false;
  }

  auto& h = hid::UsbHid::instance();

  // Mac Vulcan → Force Quit (⌘⌥Esc)
  if (eqIgnoreCase(cmd, "//vulcanmac") || eqIgnoreCase(cmd, "//vulcan-mac")) {
    h.keyDown(KEY_LEFT_GUI);  // Command
    h.keyDown(KEY_LEFT_ALT);  // Option
    delay(20);
    h.keyTap(KEY_ESC);
    delay(20);
    h.keyUp(KEY_LEFT_ALT);
    h.keyUp(KEY_LEFT_GUI);
    notifyLine("ok vulcanmac\n");
    return true;
  }

  // PC Vulcan → Ctrl+Alt+Delete
  if (eqIgnoreCase(cmd, "//vulcanpc") || eqIgnoreCase(cmd, "//vulcan-pc")) {
    h.keyDown(KEY_LEFT_CTRL);
    h.keyDown(KEY_LEFT_ALT);
    delay(20);
    h.keyTap(KEY_DELETE);
    delay(20);
    h.keyUp(KEY_LEFT_ALT);
    h.keyUp(KEY_LEFT_CTRL);
    notifyLine("ok vulcanpc\n");
    return true;
  }

  int key = -1;

  if (eqIgnoreCase(cmd, "//enter") || eqIgnoreCase(cmd, "//return") ||
      eqIgnoreCase(cmd, "//ret")) {
    key = KEY_RETURN;
  } else if (eqIgnoreCase(cmd, "//esc") || eqIgnoreCase(cmd, "//escape")) {
    key = KEY_ESC;
  } else if (eqIgnoreCase(cmd, "//tab")) {
    key = KEY_TAB;
  } else if (eqIgnoreCase(cmd, "//bs") || eqIgnoreCase(cmd, "//back") ||
             eqIgnoreCase(cmd, "//backspace")) {
    key = KEY_BACKSPACE;
  } else if (eqIgnoreCase(cmd, "//del") || eqIgnoreCase(cmd, "//delete")) {
    key = KEY_DELETE;
  } else if (eqIgnoreCase(cmd, "//space") || eqIgnoreCase(cmd, "//sp")) {
    key = ' ';
  } else if (eqIgnoreCase(cmd, "//up")) {
    key = KEY_UP_ARROW;
  } else if (eqIgnoreCase(cmd, "//down")) {
    key = KEY_DOWN_ARROW;
  } else if (eqIgnoreCase(cmd, "//left")) {
    key = KEY_LEFT_ARROW;
  } else if (eqIgnoreCase(cmd, "//right")) {
    key = KEY_RIGHT_ARROW;
  } else {
    return false;
  }

  h.keyTap(static_cast<uint8_t>(key));
  notifyLine("ok\n");
  return true;
}

bool BleSerialHidServer::handleMouseChar(char c) {
  auto& h = hid::UsbHid::instance();
  const int8_t step = storage::ConfigStore::instance().bleSerialMouseStep();
  switch (c) {
    case 'w':
    case 'W':
      h.mouseMove(0, static_cast<int8_t>(-step));
      return true;
    case 's':
    case 'S':
      h.mouseMove(0, step);
      return true;
    case 'a':
    case 'A':
      h.mouseMove(static_cast<int8_t>(-step), 0);
      return true;
    case 'd':
    case 'D':
      h.mouseMove(step, 0);
      return true;
    case 'q':
    case 'Q':
      h.mousePress(MOUSE_LEFT);
      delay(20);
      h.mouseRelease(MOUSE_LEFT);
      return true;
    case 'e':
    case 'E':
      h.mousePress(MOUSE_RIGHT);
      delay(20);
      h.mouseRelease(MOUSE_RIGHT);
      return true;
    default:
      return false;
  }
}

void BleSerialHidServer::processKeyLine(const char* line) {
  if (!line) {
    return;
  }
  if (!line[0]) {
    // empty line → Enter key
    hid::UsbHid::instance().keyTap(KEY_RETURN);
    notifyLine("ok\n");
    return;
  }
  hid::UsbHid::instance().typeText(line);
  notifyLine("ok\n");
}

void BleSerialHidServer::processMenuLine(const char* line) {
  if (!line || !line[0]) {
    return;
  }
  if (line[0] == '{') {
    hid::HidProtocol::processMessage(line);
    notifyLine("ok\n");
    return;
  }
  notifyLine("err: //key //mouse\n");
}

void BleSerialHidServer::flushLine() {
  line_buf_[line_len_] = '\0';
  const char* line = line_buf_;
  while (*line == ' ' || *line == '\t') {
    ++line;
  }

  if (tryMagic(line)) {
    line_len_ = 0;
    collecting_cmd_ = false;
    return;
  }

  switch (mode_) {
    case SerialMode::Key:
      processKeyLine(line);
      break;
    case SerialMode::Mouse: {
      bool any = false;
      for (const char* p = line; *p; ++p) {
        any = handleMouseChar(*p) || any;
      }
      if (any) {
        notifyLine("ok\n");
      }
      break;
    }
    default:
      processMenuLine(line);
      break;
  }

  line_len_ = 0;
  collecting_cmd_ = false;
}

void BleSerialHidServer::feedBytes(const uint8_t* data, size_t len) {
  if (!data || len == 0) {
    return;
  }

  bool mouse_acted = false;

  for (size_t i = 0; i < len; ++i) {
    const char c = static_cast<char>(data[i]);
    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      flushLine();
      continue;
    }

    // Mouse mode: WASD/QE act immediately unless starting a // command
    if (mode_ == SerialMode::Mouse && !collecting_cmd_) {
      if (c == '/' && line_len_ == 0) {
        collecting_cmd_ = true;
        line_buf_[line_len_++] = c;
        continue;
      }
      if (handleMouseChar(c)) {
        mouse_acted = true;
      }
      continue;
    }

    // False start: "/w" in mouse mode → treat as mouse, not a command
    if (mode_ == SerialMode::Mouse && collecting_cmd_ && line_len_ == 1 && c != '/') {
      collecting_cmd_ = false;
      line_len_ = 0;
      if (handleMouseChar(c)) {
        mouse_acted = true;
      }
      continue;
    }

    if (line_len_ + 1 < sizeof(line_buf_)) {
      line_buf_[line_len_++] = c;
    } else {
      line_len_ = 0;
      collecting_cmd_ = false;
    }
  }

  // Always terminate before string compares
  if (line_len_ > 0) {
    line_buf_[line_len_] = '\0';
  }

  // Complete JSON in one write
  if (line_len_ > 0 && line_buf_[0] == '{' && line_buf_[line_len_ - 1] == '}') {
    flushLine();
    return;
  }

  // Complete magic word with no newline (common on iOS BLE apps)
  if (line_len_ > 0 && tryMagic(line_buf_)) {
    line_len_ = 0;
    collecting_cmd_ = false;
    return;
  }

  // App sent a full line with no newline
  if (line_len_ > 0 && line_buf_[0] != '/') {
    if (mode_ == SerialMode::Menu && line_buf_[0] != '{') {
      flushLine();
    } else if (mode_ == SerialMode::Key) {
      flushLine();
    }
  }

  if (mouse_acted) {
    notifyLine("ok\n");
  }
}

void BleSerialHidServer::begin() {
  if (running_) {
    return;
  }

  g_serial = this;
  clients_ = 0;
  line_len_ = 0;
  collecting_cmd_ = false;
  welcome_sent_ = false;
  welcome_at_ms_ = 0;
  mode_ = SerialMode::Menu;

  auto& cfg = storage::ConfigStore::instance();
  cfg.loadSettings();
  cfg.loadBluetooth();
  cfg.loadMouse();
  BLEDevice::init(cfg.bleSerialDeviceName());
  g_server = BLEDevice::createServer();
  g_server->setCallbacks(&g_server_cb);

  BLEService* service = g_server->createService(cfg.bleNusServiceUuid());

  g_rx = service->createCharacteristic(
      cfg.bleNusRxUuid(),
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  g_rx->setCallbacks(&g_rx_cb);

  g_tx = service->createCharacteristic(
      cfg.bleNusTxUuid(),
      BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  g_cccd = new BLE2902();
  g_cccd->setCallbacks(&g_cccd_cb);
  g_tx->addDescriptor(g_cccd);

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(cfg.bleNusServiceUuid());
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  g_advertising = true;

  hid::UsbHid::instance().begin();
  running_ = true;
}

void BleSerialHidServer::loop() {
  if (welcome_at_ms_ != 0 && clients_ > 0 && millis() >= welcome_at_ms_) {
    welcome_at_ms_ = 0;
    sendWelcome();
  }
}

void BleSerialHidServer::end() {
  if (!running_) {
    return;
  }

  g_advertising = false;
  if (BLEDevice::getAdvertising()) {
    BLEDevice::getAdvertising()->stop();
  }
  BLEDevice::deinit(true);
  g_server = nullptr;
  g_rx = nullptr;
  g_tx = nullptr;
  g_cccd = nullptr;
  g_serial = nullptr;
  clients_ = 0;
  line_len_ = 0;
  collecting_cmd_ = false;
  welcome_sent_ = false;
  welcome_at_ms_ = 0;
  mode_ = SerialMode::Menu;
  running_ = false;
}

}  // namespace bt_serial_hid
