# PocketOps

<p align="center">
  <img src="docs/images/hardware-tdongle-s3.png" alt="LilyGO T-Dongle-S3" width="320"/>
</p>

<p align="center">
  <strong>Firmware v0.1.0</strong> · LilyGO <a href="https://lilygo.cc/products/t-dongle-s3">T-Dongle-S3</a><br/>
  USB stick · tiny screen · remote HID · microSD tool kit
</p>

---

Plug it into USB-A, pick a tool on the screen, and use it as a remote keyboard/mouse (Wi‑Fi or Bluetooth). Flip **Expose** on and the host sees a `POCKETOPS` drive packed with field tools.

**Plain English:** a smart USB stick with a screen. Your phone can drive the mouse/keyboard; the SD card can show up on the PC like a normal flash drive when you want it.

<p align="center">
  <img src="docs/images/hardware-tdongle-s3-angle.png" alt="T-Dongle-S3 angled view" width="240"/>
  &nbsp;
  <img src="docs/images/hardware-tdongle-s3-screen.jpg" alt="T-Dongle-S3 with display" width="240"/>
  &nbsp;
  <img src="docs/images/hardware-tdongle-s3-shop.jpg" alt="T-Dongle-S3 product shot" width="240"/>
</p>

---

## On-screen UI

160×80 ST7735 — frames match the live menu tree (scaled for the README).

### Boot & main menu

Four tools on the root list: **WiFi Remote HID** · **BT Remote HID** · **BT Serial HID** · **TF Card**.

| Splash | Menu (WiFi selected) | Menu (BT Serial) | Menu (TF Card) |
| :---: | :---: | :---: | :---: |
| <img src="docs/images/ui-splash.png" alt="Boot splash" width="200"/> | <img src="docs/images/ui-menu.png" alt="Main menu" width="200"/> | <img src="docs/images/ui-menu-serial.png" alt="BT Serial selected" width="200"/> | <img src="docs/images/ui-menu-tf.png" alt="TF Card selected" width="200"/> |

### HID tools

| WiFi Remote HID | BT Remote HID | BT Serial HID |
| :---: | :---: | :---: |
| <img src="docs/images/ui-wifi-hid.png" alt="WiFi Remote HID" width="240"/> | <img src="docs/images/ui-bt-remote.png" alt="BT Remote HID" width="240"/> | <img src="docs/images/ui-bt-serial.png" alt="BT Serial HID" width="240"/> |

### TF Card tool

Submenu: **Expose** · **Check card** · **Format card**.

| Submenu | Expose OFF | Expose ON |
| :---: | :---: | :---: |
| <img src="docs/images/ui-tf-submenu.png" alt="TF Card submenu" width="240"/> | <img src="docs/images/ui-expose-off.png" alt="Expose OFF" width="240"/> | <img src="docs/images/ui-expose-on.png" alt="Expose ON" width="240"/> |

| Check card | Format confirm | Format OK |
| :---: | :---: | :---: |
| <img src="docs/images/ui-check.png" alt="Check card" width="240"/> | <img src="docs/images/ui-format-confirm.png" alt="Format confirm" width="240"/> | <img src="docs/images/ui-format-ok.png" alt="Format OK" width="240"/> |

> Cyan **SD** badge = Expose on (LED stays red — by design). Yellow flash = card busy over USB. Footer `1dn 2up holdOK 3bk` = BOOT gestures.

---

## Hardware

| Item | Detail |
| --- | --- |
| Board | LilyGO **T-Dongle-S3** (ESP32-S3, 16 MB flash, **no PSRAM**) |
| Display | 0.96″ ST7735 SPI, **160 × 80** |
| LED | APA102 RGB (soft-bitbang; LCD SPI stays free) |
| Storage | microSD via **SDMMC** |
| Button | **BOOT** (GPIO0) — only physical control |
| USB | TinyUSB — CDC serial + HID keyboard/mouse + optional MSC |

Full pin map, schematics, and LilyGO links: [`reference/`](reference/README.md).

---

## Tools

Registered at boot in `src/app/App.cpp`:

| Menu name | What it does |
| --- | --- |
| **WiFi Remote HID** | SoftAP + web UI. Join the AP and drive USB keyboard/mouse over WebSocket. |
| **BT Remote HID** | BLE + Web Bluetooth page (`web/bt-remote.html`). Desktop Chrome (HTTPS). |
| **BT Serial HID** | Nordic UART Service. iOS / serial BLE apps use `//key` / `//mouse` modes. |
| **TF Card** | **Expose** · **Check card** · **Format card** |

### SoftAP / BLE defaults (`include/config.h`)

| Setting | Default |
| --- | --- |
| SSID | `PocketOps` |
| Password | `pocketops` |
| HTTP / WebSocket | **80** / **81** |
| BLE names | `PocketOps` · `PocketOps Serial` |

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

## Status lights & SD badge

The RGB LED is a **mode indicator for the HID tools only**. **TF Card / Expose does not change the LED** — it stays on the default red pulse (same as the menu). Expose status is shown only by the on-screen **SD** badge.

| RGB LED | Meaning |
| --- | --- |
| **Red** pulse | Powered / menu / **TF Card** (including Expose on or off) |
| **Sky blue** pulse | WiFi Remote HID |
| **Violet** pulse | BT Remote HID |
| **Green** pulse | BT Serial HID |

| On-screen SD badge | Meaning |
| --- | --- |
| Cyan **SD** | Expose **on** (host can see the card) |
| Yellow flashing **SD** | Expose on **and** recent SD R/W over USB |
| No badge colour pop | Expose **off** |

**Plain English:** coloured LED = which HID mode you’re in. Red LED during Expose is normal — watch the tiny **SD** badge for drive on/off. Yellow flash = the Mac is still reading/writing the card (Finder/Spotlight can keep that going after a sync).

Expose on/off is saved in flash and restored on boot.

---

## TF Card / USB drive

### Expose

1. Insert microSD  
2. **TF Card → Expose**  
3. **1×** toggles ON / OFF  
4. Host mounts FAT volume **`POCKETOPS`**

Expose-on uses a soft USB reconnect (no full reboot — Wi‑Fi/BLE stay up).

### Check card

Type, size, free space, and whether PocketOps folders exist.

### Format card

**Wipes the whole card.** Two confirms, then:

1. Eject path if Expose was on  
2. FAT format, label **`POCKETOPS`**  
3. Creates `/Mac` `/PC` `/Linux` `/Logs` `/Audio`

Turn Expose back on after format to remount on the Mac.

---

## Card kit (`sdcard-layout/`)

Folder skeleton is in git; downloaded binaries are **gitignored**. Sync onto a real card with the script.

```
sdcard-layout/
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
```

### Sync kit → stick

Needs **Expose ON** and a volume labeled **`POCKETOPS`**. Unchanged files are skipped.

**macOS**

```bash
./scripts/sync-to-pocketops.sh
```

**Windows** (double-click or from a prompt)

```bat
scripts\sync-to-pocketops.cmd
```

```powershell
powershell -ExecutionPolicy Bypass -File scripts\sync-to-pocketops.ps1
```

Uses `robocopy /FFT` (FAT 2-second time fuzz) so the stick isn’t fully re-copied every run.

---

## Build & flash

```bash
pio run -e T-Dongle-S3              # build
pio run -e T-Dongle-S3 -t upload    # flash
pio device monitor -e T-Dongle-S3   # 115200
```

If upload fails: **hold BOOT while plugging in**, then upload.

`platformio.ini` forces **TinyUSB** (`ARDUINO_USB_MODE=0`) for HID + CDC — leave that alone if you want keyboard/mouse.

---

## Project layout

```
PocketOps/
├── platformio.ini
├── boards/dongles3.json
├── include/                 # board_pins.h, config.h
├── src/
│   ├── app/ display/ input/ led/ hid/
│   ├── storage/             # TfExpose MSC + format
│   ├── ui/                  # main menu
│   └── tools/               # one folder per tool
├── scripts/sync-to-pocketops.sh|.ps1|.cmd
├── sdcard-layout/
├── docs/images/             # README hardware + UI frames
├── web/bt-remote.html
└── reference/               # LilyGO hardware pack
```

### Adding a tool

1. Subclass `tools::Tool` under `src/tools/<name>/`  
2. Register it in `App.cpp` → `registerTools()`  
3. Implement `onEnter` / `onExit` / `loop` / `draw`

---

## Config cheat sheet

| Symbol | Role |
| --- | --- |
| `FIRMWARE_VERSION` / `SPLASH_MS` | Splash + Serial |
| `WIFI_AP_*` / `WEB_PORT` / `WS_PORT` | Wi‑Fi Remote HID |
| `BLE_*` / `BLE_SERIAL_*` / `BLE_NUS_*` | BLE tools |
| `BTN_*` | Debounce / multi-tap / long-press |

Pins: `include/board_pins.h`.

---

## Git

Ignored: `.pio`, `.venv/`, VS Code noise, macOS junk, **kit binaries** under `sdcard-layout/**` (folders + `.gitkeep` kept), bulky **`reference/`** scrape (keeps `reference/README.md`).

---

## Safety

- **Format** erases the whole microSD.  
- HID tools are a real keyboard/mouse — only on machines you may control.  
- SoftAP password is a lab default; change it in `config.h` if needed.  
- Respect vendor licenses for anything you drop into `sdcard-layout/`.

---

## Version

| Version | Notes |
| --- | --- |
| **0.1.0** | Splash; TF Expose/Check/Format (`POCKETOPS` + Mac/PC/Linux/Logs/Audio); Wi‑Fi + BLE HID; SD badge; sync script |

## References

- [`reference/README.md`](reference/README.md) — hardware pack  
- [LilyGO wiki](https://wiki.lilygo.cc/products/t-dongle-series/t-dongle-s3/)  
- [T-Dongle-S3 GitHub](https://github.com/Xinyuan-LilyGO/T-Dongle-S3)
