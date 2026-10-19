#pragma once

namespace system_info {

// ESP32-S3 (and S2/C3) on-die sensor via Arduino temperatureRead().
// Returns false if the reading is unavailable / NaN.
// Note: chip (silicon) temperature — not ambient air.
bool chipTempC(float& out_c);

}  // namespace system_info
