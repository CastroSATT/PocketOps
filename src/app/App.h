#pragma once

#include "input/Button.h"
#include "ui/Menu.h"

namespace app {

enum class State : uint8_t {
  Menu,
  Tool,
};

class App {
 public:
  App();
  void begin();
  void loop();

 private:
  void onButton(input::Event event);
  void enterSelectedTool();
  void leaveTool();
  void syncMenuLed();

  input::Button button_;
  ui::Menu menu_;
  State state_ = State::Menu;
  tools::Tool* active_ = nullptr;
};

}  // namespace app
