#pragma once

#include <Arduino.h>
#include <functional>

namespace input {

enum class Event : uint8_t {
  None = 0,
  Single,   // 1 press  → down / next
  Double,   // 2 press  → up / prev
  Triple,   // 3 press  → back
  Long,     // hold     → enter / select
};

using EventCallback = std::function<void(Event)>;

class Button {
 public:
  explicit Button(int pin);

  void begin();
  void update();  // call every loop
  void onEvent(EventCallback cb);

 private:
  void emit(Event e);
  void finishTapSequence();

  int pin_;
  EventCallback cb_;

  bool stable_down_ = false;
  bool raw_down_ = false;
  uint32_t last_change_ms_ = 0;
  uint32_t down_at_ms_ = 0;

  uint8_t tap_count_ = 0;
  uint32_t last_tap_ms_ = 0;
  bool long_fired_ = false;
};

const char* toString(Event e);

}  // namespace input
