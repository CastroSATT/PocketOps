#pragma once

#include <stdint.h>
#include <string.h>

// Shared Wi-Fi mode colours for RGB LED, TFT badge, and web header badge.
// Keep these RGB triples identical across all three surfaces.
namespace wifi_mode_ui {

inline void rgb(const char* mode, uint8_t& r, uint8_t& g, uint8_t& b) {
  if (mode && strcmp(mode, "apsta") == 0) {
    // SoftAP + join solid colour (badge/LED use rgbLed to alternate AP/STA instead)
    r = 0;
    g = 220;
    b = 180;
  } else if (mode && strcmp(mode, "sta") == 0) {
    // Join only — amber
    r = 255;
    g = 140;
    b = 40;
  } else {
    // SoftAP only — sky blue (WiFi Remote HID legacy colour)
    r = 0;
    g = 140;
    b = 255;
  }
}

// APA102 / badge: AP sky / STA amber / both-live alternates (phase 0=AP, 1=STA).
inline void rgbLed(const char* mode, uint8_t phase, uint8_t& r, uint8_t& g, uint8_t& b) {
  if (mode && strcmp(mode, "apsta") == 0) {
    rgb((phase & 1) ? "sta" : "ap", r, g, b);
    return;
  }
  rgb(mode, r, g, b);
}

// Live colour/tag mode from config + radio: "ap" | "sta" | "apsta" | "" (hide).
// SoftAP+join config only becomes "apsta" when SoftAP and STA are both up.
inline const char* effectiveMode(const char* mode, bool ap_up, bool sta_up) {
  if (!mode || !mode[0]) {
    return "";
  }
  if (strcmp(mode, "apsta") == 0) {
    if (ap_up && sta_up) {
      return "apsta";
    }
    if (ap_up) {
      return "ap";
    }
    if (sta_up) {
      return "sta";
    }
    return "";
  }
  if (strcmp(mode, "ap") == 0) {
    return ap_up ? "ap" : "";
  }
  if (strcmp(mode, "sta") == 0) {
    return sta_up ? "sta" : "";
  }
  return "";
}

inline const char* tag(const char* mode) {
  if (mode && strcmp(mode, "apsta") == 0) {
    return "AP+";
  }
  if (mode && strcmp(mode, "sta") == 0) {
    return "STA";
  }
  if (mode && strcmp(mode, "ap") == 0) {
    return "AP";
  }
  return "";
}

inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

}  // namespace wifi_mode_ui
