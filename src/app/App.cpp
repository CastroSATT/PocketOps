#include "app/App.h"

#include "display/Display.h"
#include "led/RgbLed.h"
#include "Branding.h"
#include "storage/BootPrefs.h"
#include "storage/ConfigStore.h"
#include "storage/DeviceLog.h"
#include "storage/FirmwareUpdate.h"
#include "storage/TfExpose.h"
#include "storage/UsbConsole.h"
#include "tools/ToolRegistry.h"
#include "tools/bt_remote_hid/BtRemoteHidTool.h"
#include "tools/bt_serial_hid/BtSerialHidTool.h"
#include "tools/remote_hid/RemoteHidTool.h"
#include "tools/tf_card/TfCardTool.h"

namespace {

tools::RemoteHidTool g_remote_hid;
tools::BtRemoteHidTool g_bt_remote_hid;
tools::BtSerialHidTool g_bt_serial_hid;
tools::TfCardTool g_tf_card;

void registerTools() {
  auto& reg = tools::ToolRegistry::instance();
  reg.add(&g_remote_hid);
  reg.add(&g_bt_remote_hid);
  reg.add(&g_bt_serial_hid);
  reg.add(&g_tf_card);
}

}  // namespace

namespace app {

App::App() : button_(-1) {}

void App::begin() {
  Serial.begin(115200);
  delay(300);

  auto& cfg = storage::ConfigStore::instance();
  cfg.begin();
  storage::UsbConsole::instance().begin();
  cfg.loadSettings();
  cfg.loadBluetooth();
  cfg.loadMouse();
  cfg.loadKeyboard();
  cfg.loadFormatFolders();
  cfg.loadUi();
  cfg.loadDisplay();
  storage::FirmwareUpdate::instance().begin();
  storage::DeviceLog::instance().begin();
  PO_LOGI("boot", "%s v%s", cfg.productName(), branding::kFirmwareVersion);

  led::RgbLed::instance().begin();
  display::begin();
  display::applyFromConfig();
  led::RgbLed::instance().setBrightnessPercent(cfg.lightBrightness());
  display::splash();
  delay(cfg.splashMs());

  registerTools();
  storage::BootPrefs::instance().begin();
  // Restore "Expose on boot" preference (live Expose is separate).
  storage::TfExpose::instance().loadSaved();
  PO_LOGI("sd", "expose %s boot=%d present=%d",
          storage::TfExpose::instance().enabled() ? "on" : "off",
          storage::TfExpose::instance().bootEnabled() ? 1 : 0,
          storage::TfExpose::instance().cardPresent() ? 1 : 0);

  button_.begin();
  button_.onEvent([this](input::Event e) { onButton(e); });

  menu_.begin();
  state_ = State::Menu;
  active_ = nullptr;
  syncMenuLed();
  display::noteActivity();

  PO_LOGI("boot", "tools=%d", tools::ToolRegistry::instance().count());

  // Optional auto-enter tool (Config → UI → Tool on boot).
  const char* boot_id = storage::BootPrefs::instance().toolId();
  if (boot_id && boot_id[0]) {
    tools::Tool* t = tools::ToolRegistry::instance().findById(boot_id);
    if (t) {
      enterTool(t);
    } else {
      PO_LOGW("boot", "unknown boot tool id=%s", boot_id);
    }
  }
}

void App::syncMenuLed() {
  auto& rgb = led::RgbLed::instance();
  // Red = powered / idle (menu + tools without their own colour, e.g. TF Card)
  if (state_ != State::Tool || !active_) {
    rgb.pulse(255, 0, 0);
    return;
  }
  uint8_t r = 0, g = 0, b = 0;
  if (active_->menuLedPulse(r, g, b)) {
    rgb.pulse(r, g, b);
  } else {
    rgb.pulse(255, 0, 0);
  }
}

void App::loop() {
  button_.update();

  if (state_ == State::Tool && active_) {
    active_->loop();
  }

  led::RgbLed::instance().update();
  storage::TfExpose::instance().tick();
  display::tickPower();
  if (!display::asleep()) {
    display::tickStatusIcons();
  }
}

void App::onButton(input::Event event) {
  PoSer().printf("btn %s\n", input::toString(event));
  display::noteActivity();

  if (state_ == State::Menu) {
    switch (event) {
      case input::Event::Single:
        menu_.moveDown();
        syncMenuLed();
        break;
      case input::Event::Double:
        menu_.moveUp();
        syncMenuLed();
        break;
      case input::Event::Long:
        enterSelectedTool();
        break;
      case input::Event::Triple:
        // already at root
        break;
      default:
        break;
    }
    return;
  }

  // Tool active
  if (event == input::Event::Triple) {
    if (active_ && active_->onBack()) {
      return;
    }
    leaveTool();
    return;
  }

  if (active_) {
    active_->onButton(event);
  }
}

void App::enterSelectedTool() { enterTool(menu_.selected()); }

void App::enterTool(tools::Tool* tool) {
  if (!tool) {
    return;
  }
  active_ = tool;
  state_ = State::Tool;
  PO_LOGI("tool", "enter %s", tool->id());
  active_->onEnter();
  syncMenuLed();
}

void App::leaveTool() {
  if (active_) {
    PO_LOGI("tool", "leave %s", active_->id());
    active_->onExit();
  }
  active_ = nullptr;
  state_ = State::Menu;
  menu_.draw();
  syncMenuLed();
}

}  // namespace app
