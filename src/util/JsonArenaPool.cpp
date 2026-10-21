#include "util/JsonArenaPool.h"

#include <stdlib.h>

namespace util {
namespace {

char* g_buf = nullptr;
size_t g_cap = 0;
bool g_busy = false;

}  // namespace

char* acquireJsonArena(size_t need) {
  if (g_busy) {
    return nullptr;
  }
  if (need < 64) {
    need = 64;
  }
  if (!g_buf || g_cap < need) {
    char* n = static_cast<char*>(realloc(g_buf, need));
    if (!n) {
      return nullptr;
    }
    g_buf = n;
    g_cap = need;
  }
  g_busy = true;
  g_buf[0] = '\0';
  return g_buf;
}

void releaseJsonArena(char* p) {
  if (!p) {
    return;
  }
  if (p == g_buf) {
    g_busy = false;
  }
}

}  // namespace util
