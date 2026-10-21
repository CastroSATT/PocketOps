#pragma once

namespace storage {

/** Shared LAN host card — live scan + saved profiles use the same fields. */
struct LanHost {
  char ip[16] = {};
  char mac[18] = {};
  char hostname[40] = {};
  char vendor[24] = {};
  char notes[64] = {};
  char open_ports[96] = {};
  int rtt_ms = -1;
};

}  // namespace storage
