#pragma once

// Runtime settings live on LittleFS under data/config/:
//   pocketops.ini          — Wi‑Fi / BT / UUIDs
//   pocketops_folders.ini  — SD drive name + Format folders
//   pocketops_ui.ini       — splash text + button timing
//   pocketops_display.ini  — TFT / LED brightness + screen sleep
//   pocketops_keyboard.ini — typing character interval
//   ../www/index.html      — Remote HID web UI (LittleFS /www/index.html)
//
// Compile-time only (rebuild + upload firmware):
//   config/board.ini       — GPIO + TFT → scripts/apply_board_config.py
//   include/Branding.h     — name, version, remote title, BT serial banner
//
// Firmware seeds missing LittleFS files, then always reads from them.
// See storage::ConfigStore and scripts/embed_www_seed.py
