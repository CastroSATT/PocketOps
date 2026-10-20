#include "util/JsonEsc.h"

namespace util {

void jsonEscape(const char* in, char* out, size_t out_len) {
  if (!out || out_len == 0) {
    return;
  }
  size_t o = 0;
  if (!in) {
    out[0] = '\0';
    return;
  }
  for (size_t i = 0; in[i] && o + 2 < out_len; ++i) {
    const char c = in[i];
    if (c == '"' || c == '\\') {
      out[o++] = '\\';
      out[o++] = c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      continue;
    } else {
      out[o++] = c;
    }
  }
  out[o] = '\0';
}

}  // namespace util
