#include "laser_limits.h"

#include <cmath>

namespace stagecore::stagelaser {

bool limits_valid(const Limits &limits) {
  if (limits.pulse_ms == 0 || limits.pulse_ms > 10000) return false;
  if (limits.min_rest_ms > 60000) return false;
  if (!std::isfinite(limits.min_flash_hz) ||
      !std::isfinite(limits.max_flash_hz) ||
      limits.min_flash_hz <= 0.0 ||
      limits.max_flash_hz <= 0.0 ||
      limits.min_flash_hz > limits.max_flash_hz) {
    return false;
  }
  if (limits.max_flash_duration_ms == 0 ||
      limits.max_flash_duration_ms > 3600000) {
    return false;
  }
  return true;
}

}  // namespace stagecore::stagelaser
