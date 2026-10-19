#pragma once

#include <Arduino.h>

namespace led {

// Onboard APA102 (DIN=40, CLK=39, BGR). Soft-bitbang — keeps LCD SPI free.
class RgbLed {
 public:
  static RgbLed& instance();

  void begin();
  void off();
  void setRgb(uint8_t r, uint8_t g, uint8_t b);

  // Smooth breathe of a fixed hue. Call from App::loop.
  void pulse(uint8_t r, uint8_t g, uint8_t b);
  void update();  // continues last pulse/off mode

  void setPulseColor(uint8_t r, uint8_t g, uint8_t b);
  void setPulseEnabled(bool on);

  // Global APA102 brightness (percent · 5–100). Maps to 5-bit hardware level.
  void setBrightnessPercent(uint8_t percent);
  uint8_t brightnessPercent() const { return brightness_pct_; }

 private:
  RgbLed() = default;
  void show(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);
  void writeByte(uint8_t v);
  void clockPulse();
  uint8_t hwBrightness() const;

  bool begun_ = false;
  bool pulse_on_ = false;
  uint8_t pr_ = 0, pg_ = 0, pb_ = 0;
  uint8_t brightness_pct_ = 44;
  uint8_t last_r_ = 255, last_g_ = 255, last_b_ = 255, last_br_ = 255;
};

}  // namespace led
