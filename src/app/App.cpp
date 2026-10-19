#include "app/App.h"

#include "board_pins.h"
#include "config.h"
#include "display/Display.h"
#include "led/RgbLed.h"
#include "storage/TfExpose.h"
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

App::App() : button_(pins::BOOT_BTN) {}

void App::begin() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("PocketOps boot v%s\n", config::FIRMWARE_VERSION);

  display::begin();
  display::splash();
  delay(config::SPLASH_MS);

  led::RgbLed::instance().begin();
  registerTools();
  // Register MSC interface, then restore saved TF expose on/off.
  storage::TfExpose::instance().loadSaved();

  button_.begin();
  button_.onEvent([this](input::Event e) { onButton(e); });

  menu_.begin();
  state_ = State::Menu;
  active_ = nullptr;
  syncMenuLed();

  Serial.printf("Tools registered: %d\n", tools::ToolRegistry::instance().count());
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
  display::tickStatusIcons();
}

void App::onButton(input::Event event) {
  Serial.printf("btn %s\n", input::toString(event));

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

void App::enterSelectedTool() {
  tools::Tool* tool = menu_.selected();
  if (!tool) {
    return;
  }
  active_ = tool;
  state_ = State::Tool;
  Serial.printf("enter tool: %s\n", tool->id());
  active_->onEnter();
  syncMenuLed();
}

void App::leaveTool() {
  if (active_) {
    Serial.printf("leave tool: %s\n", active_->id());
    active_->onExit();
  }
  active_ = nullptr;
  state_ = State::Menu;
  menu_.draw();
  syncMenuLed();
}

}  // namespace app
