#pragma once

#include <stddef.h>

namespace util {

// Trim leading/trailing space, tab, CR, LF in place.
void trimInPlace(char* s);

// Null-safe strncpy into dst (always NUL-terminated when dst_len > 0).
void copyKey(char* dst, size_t dst_len, const char* src);

}  // namespace util
