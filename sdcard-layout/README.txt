PocketOps card kit
==================

This is the layout copied onto the stick when Expose is on (volume name POCKETOPS).

Folders
-------
  Mac/      macOS apps and helpers
  PC/       Windows portable tools (see subfolders)
  Linux/    Linux scripts / AppImages / packages
  Logs/     Notes, dumps, and session logs you save in the field
  Audio/    Sound clips, voice notes, audio utilities
            (includes Audio Tests/ for speaker & sound-system tones)

On-chip config (LittleFS — pio run -t uploadfs):
  data/config/pocketops.ini          — Wi‑Fi / Bluetooth / UUIDs
  data/config/pocketops_folders.ini  — SD volume/USB names + Format folders
  data/config/pocketops_ui.ini       — splash / menu / branding
  data/www/index.html                — Remote HID web UI (LittleFS /www/)

Board GPIO + TFT (compile-time, not on the card / LittleFS):
  config/board.ini                   — edit, then rebuild + upload firmware

This card layout is what you sync afterward.

Sync from the PocketOps repo
----------------------------
  macOS:    ./scripts/sync-to-pocketops.sh
  Windows:  scripts\sync-to-pocketops.cmd

Only new or changed files are copied. README.txt files like this one stay on the
card so you know what belongs where.
