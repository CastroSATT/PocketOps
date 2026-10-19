#pragma once

// LilyGO T-Dongle-S3 pin map
// https://github.com/Xinyuan-LilyGO/T-Dongle-S3

namespace pins {

// APA102 RGB LED
constexpr int RGB_DIN = 40;
constexpr int RGB_CLK = 39;

// ST7735 LCD (SPI)
constexpr int LCD_CS = 4;
constexpr int LCD_MOSI = 3;
constexpr int LCD_SCLK = 5;
constexpr int LCD_DC = 2;
constexpr int LCD_RST = 1;
constexpr int LCD_BL = 38;

// microSD (SDMMC)
constexpr int SD_D0 = 14;
constexpr int SD_D1 = 17;
constexpr int SD_D2 = 21;
constexpr int SD_D3 = 18;
constexpr int SD_CLK = 12;
constexpr int SD_CMD = 16;

// Controls / expansion
constexpr int BOOT_BTN = 0;
constexpr int QWIIC_TX = 43;
constexpr int QWIIC_RX = 44;

}  // namespace pins
