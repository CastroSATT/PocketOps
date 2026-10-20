#include "util/TextUtil.h"

#include <cstring>

namespace util {

void trimInPlace(char* s) {
  if (!s) {
    return;
  }
  char* start = s;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
    ++start;
  }
  if (start != s) {
    memmove(s, start, strlen(start) + 1);
  }
  size_t n = strlen(s);
  while (n > 0 &&
         (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
    s[--n] = '\0';
  }
}

void copyKey(char* dst, size_t dst_len, const char* src) {
  if (!dst || dst_len == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dst_len - 1);
  dst[dst_len - 1] = '\0';
}

}  // namespace util
