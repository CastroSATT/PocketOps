#include "system/ChipTemp.h"

#include <Arduino.h>
#include <math.h>

namespace system_info {

bool chipTempC(float& out_c) {
  const float t = temperatureRead();
  if (isnan(t)) {
    return false;
  }
  out_c = t;
  return true;
}

}  // namespace system_info
