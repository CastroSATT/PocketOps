# PlatformIO pre-script: config/board.ini → TFT_eSPI flags + include/generated/BoardPins.h
# Board pins/screen are compile-time only (not on LittleFS).
Import("env")

from pathlib import Path

PROJECT_DIR = Path(env["PROJECT_DIR"])
INI_PATH = PROJECT_DIR / "config" / "board.ini"
OUT_PINS = PROJECT_DIR / "include" / "generated" / "BoardPins.h"


def parse_ini(path: Path) -> dict:
    out = {}
    if not path.is_file():
        raise SystemExit(f"board config: missing {path}")
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith(";") or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        out[key.strip()] = value.strip()
    return out


def as_int(cfg: dict, key: str, default: int) -> int:
    try:
        return int(cfg.get(key, default))
    except ValueError:
        return default


cfg = parse_ini(INI_PATH)

# --- TFT_eSPI compile defines ---
defines = [
    "USER_SETUP_LOADED=1",
    "DISABLE_ALL_LIBRARY_WARNINGS=1",
]

if as_int(cfg, "use_hspi", 1):
    defines.append("USE_HSPI_PORT=1")

driver = cfg.get("tft_driver", "ST7735").upper()
if driver == "ST7735":
    defines.append("ST7735_DRIVER=1")
else:
    raise SystemExit(f"board config: unsupported tft_driver={driver}")

# Compare case-insensitively (str.upper turns "160x80" into "160X80").
tab = cfg.get("tft_tab", "GREENTAB160x80").upper()
if tab == "GREENTAB160X80":
    defines.append("ST7735_GREENTAB160x80=1")
else:
    raise SystemExit(f"board config: unsupported tft_tab={cfg.get('tft_tab')}")

defines.extend(
    [
        f"TFT_WIDTH={as_int(cfg, 'tft_width', 80)}",
        f"TFT_HEIGHT={as_int(cfg, 'tft_height', 160)}",
        f"TFT_RGB_ORDER={as_int(cfg, 'tft_rgb_order', 1)}",
        f"TFT_MISO={as_int(cfg, 'tft_miso', -1)}",
        f"TFT_MOSI={as_int(cfg, 'lcd_mosi', 3)}",
        f"TFT_SCLK={as_int(cfg, 'lcd_sclk', 5)}",
        f"TFT_CS={as_int(cfg, 'lcd_cs', 4)}",
        f"TFT_DC={as_int(cfg, 'lcd_dc', 2)}",
        f"TFT_RST={as_int(cfg, 'lcd_rst', 1)}",
        f"SPI_FREQUENCY={as_int(cfg, 'spi_frequency', 27000000)}",
    ]
)

if as_int(cfg, "load_glcd", 1):
    defines.append("LOAD_GLCD=1")
if as_int(cfg, "load_font2", 1):
    defines.append("LOAD_FONT2=1")
if as_int(cfg, "load_font4", 1):
    defines.append("LOAD_FONT4=1")

env.Append(CPPDEFINES=defines)

# --- Runtime pin header (constexpr) ---
OUT_PINS.parent.mkdir(parents=True, exist_ok=True)
OUT_PINS.write_text(
    "// AUTO-GENERATED from config/board.ini — do not edit\n"
    "#pragma once\n"
    "\n"
    "namespace pins {\n"
    "\n"
    f"constexpr int RGB_DIN = {as_int(cfg, 'rgb_din', 40)};\n"
    f"constexpr int RGB_CLK = {as_int(cfg, 'rgb_clk', 39)};\n"
    "\n"
    f"constexpr int LCD_CS = {as_int(cfg, 'lcd_cs', 4)};\n"
    f"constexpr int LCD_MOSI = {as_int(cfg, 'lcd_mosi', 3)};\n"
    f"constexpr int LCD_SCLK = {as_int(cfg, 'lcd_sclk', 5)};\n"
    f"constexpr int LCD_DC = {as_int(cfg, 'lcd_dc', 2)};\n"
    f"constexpr int LCD_RST = {as_int(cfg, 'lcd_rst', 1)};\n"
    f"constexpr int LCD_BL = {as_int(cfg, 'lcd_bl', 38)};\n"
    f"constexpr int LCD_BL_ON_LEVEL = {as_int(cfg, 'lcd_bl_on_level', 0)};\n"
    f"constexpr int TFT_ROTATION = {as_int(cfg, 'tft_rotation', 1)};\n"
    "\n"
    f"constexpr int SD_D0 = {as_int(cfg, 'sd_d0', 14)};\n"
    f"constexpr int SD_D1 = {as_int(cfg, 'sd_d1', 17)};\n"
    f"constexpr int SD_D2 = {as_int(cfg, 'sd_d2', 21)};\n"
    f"constexpr int SD_D3 = {as_int(cfg, 'sd_d3', 18)};\n"
    f"constexpr int SD_CLK = {as_int(cfg, 'sd_clk', 12)};\n"
    f"constexpr int SD_CMD = {as_int(cfg, 'sd_cmd', 16)};\n"
    "\n"
    f"constexpr int BOOT_BTN = {as_int(cfg, 'boot_btn', 0)};\n"
    f"constexpr int QWIIC_TX = {as_int(cfg, 'qwiic_tx', 43)};\n"
    f"constexpr int QWIIC_RX = {as_int(cfg, 'qwiic_rx', 44)};\n"
    "\n"
    "}  // namespace pins\n",
    encoding="utf-8",
)

print(
    f"board config: {len(defines)} TFT flags + {OUT_PINS.relative_to(PROJECT_DIR)} "
    f"from {INI_PATH.relative_to(PROJECT_DIR)}"
)
