#include "led/RgbLed.h"

#include <math.h>

#include "board_pins.h"

namespace led {

RgbLed& RgbLed::instance() {
  static RgbLed s;
  return s;
}

void RgbLed::begin() {
  if (begun_) {
    return;
  }
  pinMode(pins::RGB_DIN, OUTPUT);
  pinMode(pins::RGB_CLK, OUTPUT);
  digitalWrite(pins::RGB_DIN, LOW);
  digitalWrite(pins::RGB_CLK, LOW);
  begun_ = true;
  off();
}

void RgbLed::clockPulse() {
  digitalWrite(pins::RGB_CLK, HIGH);
  delayMicroseconds(1);
  digitalWrite(pins::RGB_CLK, LOW);
  delayMicroseconds(1);
}

void RgbLed::writeByte(uint8_t v) {
  for (int i = 7; i >= 0; --i) {
    digitalWrite(pins::RGB_DIN, (v >> i) & 1);
    clockPulse();
  }
}

void RgbLed::show(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
  if (!begun_) {
    return;
  }
  if (r == last_r_ && g == last_g_ && b == last_b_ && brightness == last_br_) {
    return;
  }
  last_r_ = r;
  last_g_ = g;
  last_b_ = b;
  last_br_ = brightness;

  // Start frame
  for (int i = 0; i < 4; ++i) {
    writeByte(0x00);
  }
  // LED frame: 111 + 5-bit global brightness, then BGR for this board
  const uint8_t bri = static_cast<uint8_t>(0xE0 | (brightness > 31 ? 31 : brightness));
  writeByte(bri);
  writeByte(b);
  writeByte(g);
  writeByte(r);
  // End frame
  for (int i = 0; i < 4; ++i) {
    writeByte(0xFF);
  }
}

void RgbLed::off() {
  pulse_on_ = false;
  show(0, 0, 0, 0);
}

void RgbLed::setRgb(uint8_t r, uint8_t g, uint8_t b) {
  pulse_on_ = false;
  show(r, g, b, hwBrightness());
}

void RgbLed::setBrightnessPercent(uint8_t percent) {
  if (percent < 5) {
    percent = 5;
  }
  if (percent > 100) {
    percent = 100;
  }
  brightness_pct_ = percent;
  last_br_ = 255;  // force next show()
  if (pulse_on_) {
    update();
  }
}

uint8_t RgbLed::hwBrightness() const {
  // Map 5–100% → 2–31 (APA102 global brightness nibble).
  const uint16_t v = static_cast<uint16_t>(brightness_pct_);
  uint8_t hw = static_cast<uint8_t>((v * 31 + 50) / 100);
  if (hw < 2) {
    hw = 2;
  }
  if (hw > 31) {
    hw = 31;
  }
  return hw;
}

void RgbLed::setPulseColor(uint8_t r, uint8_t g, uint8_t b) {
  pr_ = r;
  pg_ = g;
  pb_ = b;
}

void RgbLed::setPulseEnabled(bool on) {
  pulse_on_ = on;
  if (!on) {
    show(0, 0, 0, 0);
  }
}

void RgbLed::pulse(uint8_t r, uint8_t g, uint8_t b) {
  setPulseColor(r, g, b);
  setPulseEnabled(true);
  update();
}

void RgbLed::update() {
  if (!begun_ || !pulse_on_) {
    return;
  }
  // ~1.6s breathe
  const float t = (millis() % 1600) / 1600.0f;
  const float wave = 0.5f + 0.5f * sinf(t * 6.2831853f);
  const float level = 0.12f + 0.88f * wave;
  show(static_cast<uint8_t>(pr_ * level), static_cast<uint8_t>(pg_ * level),
       static_cast<uint8_t>(pb_ * level), hwBrightness());
}

}  // namespace led
