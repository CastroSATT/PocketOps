#pragma once

#include <stddef.h>

namespace util {

/**
 * One reusable JSON reply slab (LAN hosts, Wi‑Fi scan/profiles, …).
 * SoftAP handlers run one at a time — acquire → fill → send → release.
 */
char* acquireJsonArena(size_t need);
void releaseJsonArena(char* p);

}  // namespace util
