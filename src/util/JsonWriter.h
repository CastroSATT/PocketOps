#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace util {

/** Heap-arena JSON builder — avoids Arduino String += fragmentation. */
struct JsonW {
  char* p = nullptr;
  size_t n = 0;
  size_t cap = 0;
  bool ok = true;

  void begin(char* buf, size_t c) {
    p = buf;
    n = 0;
    cap = c;
    ok = true;
    if (p && cap) {
      p[0] = '\0';
    }
  }

  bool put(const char* s) {
    if (!ok || !p || !s) {
      return ok;
    }
    const size_t L = strlen(s);
    if (n + L + 1 > cap) {
      ok = false;
      return false;
    }
    memcpy(p + n, s, L);
    n += L;
    p[n] = '\0';
    return true;
  }

  bool putc(char c) {
    if (!ok || !p || n + 2 > cap) {
      ok = false;
      return false;
    }
    p[n++] = c;
    p[n] = '\0';
    return true;
  }

  bool puti(long v) {
    char t[16];
    snprintf(t, sizeof(t), "%ld", v);
    return put(t);
  }

  bool putBool(bool v) { return put(v ? "true" : "false"); }
};

}  // namespace util

