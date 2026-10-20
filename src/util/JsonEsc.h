#pragma once

#include <stddef.h>

namespace util {

// Escape for JSON string values: \" \\ and drop control chars (< 0x20).
void jsonEscape(const char* in, char* out, size_t out_len);

}  // namespace util
