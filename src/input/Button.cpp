#include "input/Button.h"

#include "board_pins.h"
#include "storage/ConfigStore.h"

namespace input {

Button::Button(int pin) : pin_(pin) {}

void Button::begin() {
  if (pin_ < 0) {
    pin_ = pins::BOOT_BTN;
  }
  pinMode(pin_, INPUT_PULLUP);
  stable_down_ = digitalRead(pin_) == LOW;
  raw_down_ = stable_down_;
  last_change_ms_ = millis();
}

void Button::onEvent(EventCallback cb) { cb_ = std::move(cb); }

void Button::emit(Event e) {
  if (cb_ && e != Event::None) {
    cb_(e);
  }
}

void Button::finishTapSequence() {
  if (tap_count_ == 1) {
    emit(Event::Single);
  } else if (tap_count_ == 2) {
    emit(Event::Double);
  } else if (tap_count_ >= 3) {
    emit(Event::Triple);
  }
  tap_count_ = 0;
}

void Button::update() {
  const uint32_t now = millis();
  const bool down = digitalRead(pin_) == LOW;

  if (down != raw_down_) {
    raw_down_ = down;
    last_change_ms_ = now;
  }

  auto& cfg = storage::ConfigStore::instance();

  if ((now - last_change_ms_) < cfg.btnDebounceMs()) {
    // still bouncing
  } else if (down != stable_down_) {
    stable_down_ = down;
    if (stable_down_) {
      down_at_ms_ = now;
      long_fired_ = false;
    } else {
      // released
      if (!long_fired_) {
        tap_count_++;
        last_tap_ms_ = now;
      }
    }
  }

  if (stable_down_ && !long_fired_ && (now - down_at_ms_) >= cfg.btnLongMs()) {
    long_fired_ = true;
    tap_count_ = 0;
    emit(Event::Long);
  }

  if (!stable_down_ && tap_count_ > 0 && (now - last_tap_ms_) >= cfg.btnMultiGapMs()) {
    finishTapSequence();
  }
}

const char* toString(Event e) {
  switch (e) {
    case Event::Single:
      return "single";
    case Event::Double:
      return "double";
    case Event::Triple:
      return "triple";
    case Event::Long:
      return "long";
    default:
      return "none";
  }
}

}  // namespace input
