#pragma once

// Compile-time pin map from config/board.ini (via scripts/apply_board_config.py).
// Not stored on LittleFS — change board.ini, then rebuild + upload firmware.
#include "generated/BoardPins.h"
