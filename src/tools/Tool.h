#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "input/Button.h"

namespace tools {

// Base class for every PocketOps tool. Add new tools by subclassing + registering.
class Tool {
 public:
  virtual ~Tool() = default;

  virtual const char* id() const = 0;
  virtual const char* name() const = 0;

  // Optional menu overrides (default = name / normal colors)
  virtual const char* menuLabel() const { return name(); }
  virtual uint16_t menuFgColor(bool selected) const {
    return selected ? TFT_WHITE : TFT_LIGHTGREY;
  }

  // Menu LED pulse (APA102). Return false to keep the LED off (e.g. TF Card).
  virtual bool menuLedPulse(uint8_t& r, uint8_t& g, uint8_t& b) const {
    r = 40;
    g = 40;
    b = 40;
    return true;
  }

  // Called when menu selects this tool
  virtual void onEnter() = 0;

  // Called on back / leave
  virtual void onExit() = 0;

  // Periodic work while active
  virtual void loop() = 0;

  // Optional: tool-specific button handling (App still owns Triple=back)
  virtual void onButton(input::Event /*event*/) {}

  // Return true if the tool consumed back (e.g. nested submenu). False → leave tool.
  virtual bool onBack() { return false; }

  // Redraw tool status UI (small screen)
  virtual void draw() = 0;
};

}  // namespace tools
