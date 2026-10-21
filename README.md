# PocketOps

<p align="center">
  <img src="docs/images/hardware-tdongle-s3.png" alt="LilyGO T-Dongle-S3" width="320"/>
</p>

<p align="center">
  <strong>Firmware v0.2.2</strong> · LilyGO <a href="https://lilygo.cc/products/t-dongle-s3">T-Dongle-S3</a><br/>
  USB stick · tiny screen · remote HID · on-device config · microSD tool kit
</p>

---

Plug it into USB-A, pick a tool on the screen, and use it as a remote keyboard/mouse (Wi‑Fi or Bluetooth). Open the **web UI** for touchpad, **LAN** scan, **Files**, and **Config** (network, mouse, SD, display, logs, firmware, backup…). Flip **Expose** on and the host sees a FAT drive packed with field tools.

**Plain English:** a smart USB stick with a screen. Your phone drives the mouse/keyboard; settings live on the stick (not buried in code); the SD card can show up like a normal flash drive when you want it.

<p align="center">
  <img src="docs/images/hardware-tdongle-s3-angle.png" alt="T-Dongle-S3 angled view" width="240"/>
  &nbsp;
  <img src="docs/images/hardware-tdongle-s3-screen.jpg" alt="T-Dongle-S3 with display" width="240"/>
  &nbsp;
  <img src="docs/images/hardware-tdongle-s3-shop.jpg" alt="T-Dongle-S3 product shot" width="240"/>
</p>

---

## What’s new (v0.2.x)

| Feature | Notes |
| --- | --- |
| **LittleFS settings** | Runtime `.ini` under `data/config/` — seed once, then edit on-device or via web |
| **Modular web UI** | `data/www/` — **Remote** · **LAN** · **Files** · **Config** (lazy-loaded modules) |
| **Wi‑Fi modes** | `ap` · `sta` · `apsta` — SoftAP, join LAN, or both |
| **Wi‑Fi profiles** | Saved SSIDs/passwords, auto-join, SoftAP settings modal |
| **LAN scan** | Discover / ports / ping / mDNS / traceroute; profiles on LittleFS; heap slab while tab open |
| **Files** | Browse / upload / download / mkdir / delete on the microSD over SoftAP |
| **Mouse / keyboard tune** | Touchpad speed, two-finger scroll, typing interval |
| **Binary HID WebSocket** | Move / buttons / keys / text as compact binary frames (Wi‑Fi Remote page) |
| **SD from the web** | Expose toggle, folder list after Format, Format with progress (`/Logs` locked) |
| **Display** | TFT brightness, screen sleep, status-LED brightness |
| **Device logs** | On-chip session logs under Config → Logs |
| **Firmware / DFU** | Config → Firmware — USB DFU switch (reboot to arm; default off) |
| **Config backup** | Streamed download / restore `.pobak` (on-chip `.ini` + Wi‑Fi profiles) |
| **Chip temp** | Shown in the Remote HID web meta / status path |
| **Board config script** | `config/board.ini` → pins/TFT at build time |

---

## On-screen UI

160×80 ST7735 — frames match the live menu tree (scaled for the README).

### Boot & main menu

Four tools: **WiFi Remote HID** · **BT Remote HID** · **BT Serial HID** · **TF Card**.

| Splash | Menu (WiFi selected) | Menu (BT Serial) | Menu (TF Card) |
| :---: | :---: | :---: | :---: |
| <img src="docs/images/ui-splash.png" alt="Boot splash" width="200"/> | <img src="docs/images/ui-menu.png" alt="Main menu" width="200"/> | <img src="docs/images/ui-menu-serial.png" alt="BT Serial selected" width="200"/> | <img src="docs/images/ui-menu-tf.png" alt="TF Card selected" width="200"/> |

### HID tools

| WiFi Remote HID | BT Remote HID | BT Serial HID |
| :---: | :---: | :---: |
| <img src="docs/images/ui-wifi-hid.png" alt="WiFi Remote HID" width="240"/> | <img src="docs/images/ui-bt-remote.png" alt="BT Remote HID" width="240"/> | <img src="docs/images/ui-bt-serial.png" alt="BT Serial HID" width="240"/> |

### TF Card tool

Submenu: **Expose** · **Check card** · **Format card** (also available under web **Config → SD**).

| Submenu | Expose OFF | Expose ON |
| :---: | :---: | :---: |
| <img src="docs/images/ui-tf-submenu.png" alt="TF Card submenu" width="240"/> | <img src="docs/images/ui-expose-off.png" alt="Expose OFF" width="240"/> | <img src="docs/images/ui-expose-on.png" alt="Expose ON" width="240"/> |

| Check card | Format confirm | Format OK |
| :---: | :---: | :---: |
| <img src="docs/images/ui-check.png" alt="Check card" width="240"/> | <img src="docs/images/ui-format-confirm.png" alt="Format confirm" width="240"/> | <img src="docs/images/ui-format-ok.png" alt="Format OK" width="240"/> |

> Cyan **SD** badge = Expose on (LED stays red in TF Card — by design). Yellow flash = card busy over USB. Footer `1dn 2up holdOK 3bk` = BOOT gestures.

---

## Hardware

| Item | Detail |
| --- | --- |
| Board | LilyGO **T-Dongle-S3** (ESP32-S3, 16 MB flash, **no PSRAM**) |
| Display | 0.96″ ST7735 SPI, **160 × 80** |
| LED | APA102 RGB (soft-bitbang; LCD SPI stays free) |
| Storage | microSD via **SDMMC** + on-chip **LittleFS** (settings + web UI) |
| Button | **BOOT** (GPIO0) — only physical control |
| USB | TinyUSB — CDC serial + HID keyboard/mouse + optional MSC |

Full pin map / schematics: [`reference/`](reference/README.md) (local pack; bulky files gitignored).

---

## Tools

| Menu name | What it does |
| --- | --- |
| **WiFi Remote HID** | Starts Wi‑Fi per mode + serves web UI. Touchpad / keys over WebSocket. |
| **BT Remote HID** | BLE + Web Bluetooth page (`web/bt-remote.html` on a desktop Chrome HTTPS origin). |
| **BT Serial HID** | Nordic UART Service — `//key` / `//mouse` from serial BLE apps. |
| **TF Card** | **Expose** · **Check card** · **Format card** on the stick UI. |

---

## Web UI

With **WiFi Remote HID** running, open the URL shown on the TFT (SoftAP default `http://192.168.4.1`).

| Tab | Contents |
| --- | --- |
| **Remote** | Touchpad, mouse buttons, hotkeys, live typing (WebSocket HID; binary frames) |
| **LAN** | Host discover, port scan, ping, mDNS, traceroute, saved profiles / CSV (shown when STA is up) |
| **Files** | microSD browser (list / upload / download / mkdir / delete) |
| **Config → Network** | Wi‑Fi mode, SoftAP modal, scan/join, saved profiles |
| **Config → Bluetooth** | BLE advertise names |
| **Config → Mouse** | Touchpad / scroll speed, two-finger scroll, BT serial step |
| **Config → Keyboard** | Character interval when typing/pasting |
| **Config → SD** | Expose, card info, volume label, Format folder list, Format |
| **Config → UI** | Splash text / duration |
| **Config → Display** | TFT brightness, sleep seconds, LED brightness |
| **Config → Logs** | On-chip device session logs |
| **Config → Firmware** | USB DFU arm switch (reboot required) |
| **Config → Backup** | Download / upload `.pobak` |

Header badges: **Wi‑Fi mode** (AP / STA / AP+) and **SD** (expose), colour-matched to the stick LED/TFT.

Source lives in `data/www/` (HTML/CSS/JS modules). Build embeds a seed via `scripts/embed_www_seed.py`; flash the filesystem with **uploadfs** (see below).

---

## Wi‑Fi modes & LED colours

Set in `pocketops.ini` / Config → Network (`wifi_mode=`):

| Mode | Behaviour | LED / badge |
| --- | --- | --- |
| **ap** | SoftAP hotspot only | Sky blue |
| **sta** | Join a LAN only | Amber |
| **apsta** | SoftAP + join | Alternates sky / amber when both live |

BT Remote / BT Serial still use **violet** / **green** pulses. **TF Card** keeps the default **red** pulse — Expose is the on-screen **SD** badge only.

---

## Runtime config (LittleFS)

PlatformIO `data/` → device LittleFS. Firmware **seeds missing files once**, then always reads from them.

| File | Role |
| --- | --- |
| `data/config/pocketops.ini` | Wi‑Fi mode, SoftAP, STA last-join, HTTP/WS ports |
| `data/config/pocketops_bt.ini` | BLE device names |
| `data/config/pocketops_mouse.ini` | Touchpad / scroll / BT serial step |
| `data/config/pocketops_keyboard.ini` | Typing interval |
| `data/config/pocketops_folders.ini` | Volume label + Format folder list (`/Logs` always kept) |
| `data/config/pocketops_ui.ini` | Splash text + ms |
| `data/config/pocketops_display.ini` | Brightness + sleep + LED brightness |
| `data/config/wifi_profiles.ini` | Saved networks (auto-join) |
| `data/www/` | Remote HID web UI |

### Compile-time only (rebuild firmware)

| File | Role |
| --- | --- |
| `include/Branding.h` | Product name, **version**, remote title, BLE UUIDs, BOOT timings |
| `config/board.ini` | GPIO + TFT → `scripts/apply_board_config.py` → generated headers |

---

## Controls (one button)

Everything is **BOOT**:

| Gesture | Menu | Inside a tool |
| --- | --- | --- |
| **1×** tap | Move **down** | Tool-specific (e.g. Expose toggle) |
| **2×** tap | Move **up** | Tool-specific |
| **Hold** (~700 ms) | **Enter** / select | Confirm (e.g. Format) |
| **3×** tap | — | **Back** |

On-screen hint: `1dn 2up holdOK 3bk`.

---

## TF Card / USB drive

### Expose

1. Insert microSD  
2. **TF Card → Expose** (or web **Config → SD**)  
3. Toggle ON / OFF  
4. Host mounts FAT volume (label from `pocketops_folders.ini`, default **PocketOps** / Finder often **POCKETOPS**)

Expose-on uses a soft USB reconnect (no full reboot — Wi‑Fi/BLE stay up). State is saved in NVS and restored on boot.

### Check card

Type, size, free space, and whether configured folders exist.

### Format card

**Wipes the whole card.** Creates folders from `pocketops_folders.ini` (defaults include `/Logs`, `/Mac`, `/PC`, `/Linux`, `/Audio`, `/Audio/Audio Tests`). **`/Logs` is required** for device logs and cannot be removed from the list.

---

## Card kit (`sdcard-layout/`)

Folder skeleton + `README.txt` guides are in git; downloaded binaries are **gitignored**. Sync onto a real card with the scripts.

```
sdcard-layout/
├── README.txt
├── Mac/
├── PC/
│   ├── Remote/         AnyDesk, RustDesk, TeamViewer QS, Chrome Remote Desktop
│   ├── Network/        PuTTY, WinSCP
│   ├── Diagnostics/    CrystalDiskInfo, DevManView, HWiNFO
│   ├── Utilities/      7-Zip, Notepad++
│   └── Browsers/       Firefox
├── Linux/
├── Logs/
└── Audio/
    └── Audio Tests/    (optional test clips — see folder README)
```

### Sync kit → stick

Needs **Expose ON** and a volume labeled **`POCKETOPS`** (or your configured label). Unchanged files are skipped.

**macOS**

```bash
./scripts/sync-to-pocketops.sh
```

**Windows**

```bat
scripts\sync-to-pocketops.cmd
```

---

## Build & flash

```bash
pio run -e T-Dongle-S3                 # build firmware
pio run -e T-Dongle-S3 -t upload       # flash app
pio run -e T-Dongle-S3 -t uploadfs     # flash LittleFS (config + www)
pio device monitor -e T-Dongle-S3      # 115200
```

After changing `data/config/*` or `data/www/*`, run **uploadfs** (or both upload + uploadfs on a fresh stick).

If upload fails: **hold BOOT while plugging in**, then upload.

`platformio.ini` forces **TinyUSB** (`ARDUINO_USB_MODE=0`) for HID + CDC.

---

## Project layout

```
PocketOps/
├── platformio.ini
├── boards/dongles3.json
├── config/board.ini              # GPIO / TFT (build-time)
├── include/
│   ├── Branding.h                # version, UUIDs, button timings
│   ├── WifiModeUi.h              # shared AP/STA colours
│   ├── board_pins.h / config.h
│   └── generated/                # from board + www seed scripts
├── data/
│   ├── config/*.ini              # LittleFS settings
│   └── www/                      # Remote · LAN · Files · Config web UI
├── src/
│   ├── app/ display/ input/ led/ hid/
│   ├── http/                     # g_http + registerPhoneApi façade
│   ├── ble/                      # shared BLE GATT bootstrap (BT tools)
│   ├── storage/                  # ConfigStore (INI schema), profiles, backup, SD FS
│   ├── system/                   # ChipTemp
│   ├── util/                     # JsonWriter, JsonEsc, TextUtil, JSON arena pool
│   ├── ui/
│   └── tools/
│       ├── remote_hid/           # SoftAP server, Wi‑Fi/Config/SD APIs, HID WS
│       ├── lan_scan/             # /api/lan/* (session slab + route modules)
│       ├── bt_remote_hid/ · bt_serial_hid/ · tf_card/
├── scripts/
│   ├── sync-to-pocketops.sh|.ps1|.cmd
│   ├── apply_board_config.py
│   └── embed_www_seed.py
├── sdcard-layout/
├── docs/images/
├── web/bt-remote.html            # BT Web Bluetooth helper page
└── reference/                    # LilyGO hardware pack (local)
```

### Adding a tool

1. Subclass `tools::Tool` under `src/tools/<name>/`  
2. Register it in `App.cpp` → `registerTools()`  
3. Implement `onEnter` / `onExit` / `loop` / `draw`

---

## Git

Ignored: `.pio`, `.venv/`, VS Code generated IntelliSense, macOS junk, **kit binaries** under `sdcard-layout/**` (keeps folders, `.gitkeep`, `README.txt`), bulky **`reference/`** scrape (keeps `reference/README.md`).

---

## Safety

- **Format** erases the whole microSD.  
- HID tools are a real keyboard/mouse — only on machines you may control.  
- SoftAP password defaults to `pocketops` — change it under **Config → Network** / SoftAP modal before untrusted use.  
- `.pobak` backups include Wi‑Fi passwords — treat like a secrets file.  
- Respect vendor licenses for anything you drop into `sdcard-layout/`.

---

## Version

| Version | Notes |
| --- | --- |
| **0.1.0** | Splash; TF Expose/Check/Format; Wi‑Fi + BLE HID; SD badge; sync scripts |
| **0.2.0** | Modular web UI; Wi‑Fi modes + profiles; Config tabs; SoftAP live UX |
| **0.2.1** | Display brightness / sleep / LED; chip temp; SoftAP modal; SD `/Logs` locked; badge/LED colour sync; `.pobak` backup |
| **0.2.2** | LAN + Files tabs; device logs; DFU switch; streamed `.pobak`; binary HID WS; LAN session slab + lazy `/api/lan/*`; `/api/chrome` header poll; route/module splits; shared BLE bootstrap; INI schema |

---

## References

- [`reference/README.md`](reference/README.md) — hardware pack  
- [LilyGO wiki](https://wiki.lilygo.cc/products/t-dongle-series/t-dongle-s3/)  
- [T-Dongle-S3 GitHub](https://github.com/Xinyuan-LilyGO/T-Dongle-S3)
