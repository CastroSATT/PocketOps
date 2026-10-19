# T-Dongle S3 — Hardware Reference

ESP32-S3 USB-A dongle from LILYGO. Tiny stick PC: Wi-Fi/BLE, small color screen, RGB LED, microSD, one button.

![T-Dongle-S3](images/product/png/T-Dongle-S3.png)

---

## Product links

| What | URL |
| --- | --- |
| Official shop | https://lilygo.cc/products/t-dongle-s3 |
| Wiki / docs | https://wiki.lilygo.cc/products/t-dongle-series/t-dongle-s3/ |
| Quick start (wiki) | https://wiki.lilygo.cc/products/t-dongle-series/t-dongle-s3/quick-start.html |
| Official GitHub | https://github.com/Xinyuan-LilyGO/T-Dongle-S3 |
| Schematic (GitHub) | https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/main/schematic/T-Dongle-S3.pdf |
| Support email | orders@lilygo.cc / info@lilygo.cc |

### Related variants

| Variant | Shop / docs |
| --- | --- |
| T-Dongle-S3 (this board) | https://lilygo.cc/products/t-dongle-s3 |
| T-Dongle-S3 Dual | see `docs/t-dongle-s3-dual-quickstart.md` |
| T-Dongle-S3 Plus | see `docs/t-dongle-s3-plus-quickstart.md` |

Typical shop price (at scrape time): about **$11.99**.

---

## What it is (plain English)

A USB stick with a brain. Plug it into a computer’s USB-A port (or a powered hub), flash code, and it can show stuff on a tiny screen, blink an RGB LED, talk over Wi-Fi/Bluetooth, and optionally store files on a microSD card tucked into the USB shell.

---

## Specs

| Parameter | Value |
| --- | --- |
| MCU | ESP32-S3 (Xtensa LX7 dual-core) |
| Flash | 16 MB Quad-SPI |
| SRAM | 512 KB |
| PSRAM | **None** (must set Disabled in Arduino/PIO) |
| Wireless | Wi-Fi 802.11 b/g/n, Bluetooth 5 LE |
| Form factor | USB Type-A plug dongle |
| Display | 0.96" ST7735 SPI IPS, **160 × 80** |
| RGB LED | APA102 × 1 (BGR color order) |
| Storage | TF / microSD slot (SDMMC) |
| Button | BOOT (GPIO0) |
| Expansion | QWIIC (default = UART TX/RX) |
| USB input | 4.8 V – 5.5 V |
| USB max current | 800 mA |

### Display details

| Feature | Value |
| --- | --- |
| Driver | ST7735 (4-wire SPI) |
| Resolution | 160 × 80 |
| Size | 0.96" |
| Supply | 3.3 V |

---

## Pin map

### LCD (ST7735)

| Signal | GPIO |
| --- | --- |
| CS | 4 |
| MOSI / SDA | 3 |
| SCLK / SCL | 5 |
| DC | 2 |
| RESET | 1 |
| Backlight | 38 |

### RGB LED (APA102)

| Signal | GPIO |
| --- | --- |
| DIN (data) | 40 |
| CLK | 39 |

### microSD (SDMMC)

| Signal | GPIO |
| --- | --- |
| D0 | 14 |
| D1 | 17 |
| D2 | 21 |
| D3 | 18 |
| CLK | 12 |
| CMD | 16 |

### Button / QWIIC

| Signal | GPIO |
| --- | --- |
| BOOT button | 0 |
| QWIIC TX | 43 |
| QWIIC RX | 44 |

**QWIIC note:** wired as serial by default. For I2C, add external pull-ups on the sensor side.

![QWIIC](docs/static/QWIIC.png)

---

## Arduino IDE board settings

| Setting | Value |
| --- | --- |
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enable |
| CPU Frequency | 240 MHz (WiFi) |
| Flash Mode | QIO 80 MHz |
| Flash Size | 16 MB (128 Mb) |
| Partition Scheme | 16M Flash (3 MB APP / 9.9 MB FATFS) |
| PSRAM | **Disabled** |
| Upload Speed | 921600 |
| USB Mode | Hardware CDC and JTAG |
| Programmer | Esptool |

Board package URL:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Use ESP32 core **3.3.0+**.

### Upload tip

If upload fails: hold **BOOT**, plug into USB (download mode), flash, then unplug/replug **without** holding BOOT.

---

## Local files in this folder

```
reference/
├── README.md                          ← you are here
├── images/
│   ├── LilyGo_logo.png
│   ├── product/png/                   ← official product renders
│   └── shop/                          ← photos from lilygo.cc store page
├── docs/
│   ├── t-dongle-s3-quickstart.md      ← official quick start
│   ├── t-dongle-s3-dual-quickstart.md
│   ├── t-dongle-s3-plus-quickstart.md
│   ├── upstream-README.md
│   ├── static/QWIIC.png
│   └── images/                        ← flasher / boot UI gifs
├── schematic/
│   ├── T-Dongle-S3.pdf
│   ├── T-Dongle-S3-QWIIC.pdf
│   ├── T-Dongle-DualS3.pdf
│   └── T-Dongle-S3-Plus.pdf
└── datasheet/
    ├── ST773_datasheet.pdf
    └── APA102_2020.pdf
```

### Product photos (local)

| File | Description |
| --- | --- |
| `images/product/png/T-Dongle-S3.png` | Main product render |
| `images/product/png/T-Dongle-S3-2.png` | Alternate angle |
| `images/product/png/T-Dongle-S3-Dual.png` | Dual variant |
| `images/product/png/T-Dongle-S3-Plus.png` | Plus variant |
| `images/shop/*` | Store-page product shots |

### Schematics / datasheets (local)

| File | What |
| --- | --- |
| `schematic/T-Dongle-S3.pdf` | Main schematic |
| `schematic/T-Dongle-S3-QWIIC.pdf` | QWIIC-oriented schematic |
| `datasheet/ST773_datasheet.pdf` | Display IC |
| `datasheet/APA102_2020.pdf` | RGB LED |

---

## Upstream examples (on GitHub)

From https://github.com/Xinyuan-LilyGO/T-Dongle-S3 — useful starters:

- `factory` / `factory_screen` — factory / screen bring-up
- `hello_world` — serial sanity check
- `lvgl_test` — LVGL on the 160×80 panel
- `sd` — TF card R/W
- `qwiic` / `qwiic_i2c_scan` / `qwiic_uart_loopback` — QWIIC

LVGL hint for this panel:

```c
#define LV_COLOR_DEPTH  16
#define LV_HOR_RES_MAX  160
#define LV_VER_RES_MAX  80
```

---

## Sources

Pulled from LILYGO wiki, shop page, and [Xinyuan-LilyGO/T-Dongle-S3](https://github.com/Xinyuan-LilyGO/T-Dongle-S3) for PocketOps offline reference.
