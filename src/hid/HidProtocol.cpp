#include "hid/HidProtocol.h"

#include <Arduino.h>
#include <cstring>

#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "hid/UsbHid.h"

namespace {

int jsonInt(const char* json, const char* key, int fallback = 0) {
  char pattern[16];
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char* p = strstr(json, pattern);
  if (!p) {
    return fallback;
  }
  p += strlen(pattern);
  while (*p == ' ') {
    ++p;
  }
  return atoi(p);
}

void jsonString(const char* json, const char* key, char* out, size_t out_len) {
  out[0] = '\0';
  char pattern[16];
  snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
  const char* p = strstr(json, pattern);
  if (!p) {
    return;
  }
  p += strlen(pattern);
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < out_len) {
    if (*p == '\\' && p[1]) {
      ++p;
    }
    out[i++] = *p++;
  }
  out[i] = '\0';
}

char jsonType(const char* json) {
  const char* p = strstr(json, "\"t\":\"");
  return p ? p[5] : 0;
}

}  // namespace

namespace hid {

int HidProtocol::mapBrowserKey(int code) {
  switch (code) {
    case 8:
      return KEY_BACKSPACE;
    case 9:
      return KEY_TAB;
    case 13:
      return KEY_RETURN;
    case 27:
      return KEY_ESC;
    case 32:
      return ' ';
    case 37:
      return KEY_LEFT_ARROW;
    case 38:
      return KEY_UP_ARROW;
    case 39:
      return KEY_RIGHT_ARROW;
    case 40:
      return KEY_DOWN_ARROW;
    default:
      return -1;
  }
}

uint8_t HidProtocol::mapMouseButton(int index) {
  switch (index) {
    case 1:
      return MOUSE_RIGHT;
    case 2:
      return MOUSE_MIDDLE;
    default:
      return MOUSE_LEFT;
  }
}

void HidProtocol::processMessage(const char* json) {
  if (!json) {
    return;
  }

  auto& h = UsbHid::instance();
  const char t = jsonType(json);

  if (t == 'm') {
    const int dx = constrain(jsonInt(json, "dx"), -127, 127);
    const int dy = constrain(jsonInt(json, "dy"), -127, 127);
    h.mouseMove(static_cast<int8_t>(dx), static_cast<int8_t>(dy));
  } else if (t == 'b') {
    const uint8_t btn = mapMouseButton(jsonInt(json, "b"));
    if (jsonInt(json, "s")) {
      h.mousePress(btn);
    } else {
      h.mouseRelease(btn);
    }
  } else if (t == 'k') {
    const int mapped = mapBrowserKey(jsonInt(json, "k"));
    if (mapped >= 0) {
      h.keyTap(static_cast<uint8_t>(mapped));
    }
  } else if (t == 'x') {
    char text[180];
    jsonString(json, "v", text, sizeof(text));
    if (text[0]) {
      h.typeText(text);
    }
  }
}

}  // namespace hid
