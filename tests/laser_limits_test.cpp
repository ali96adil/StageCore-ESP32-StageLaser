#include <cassert>
#include <cmath>
#include <limits>

#include "laser_limits.h"

int main() {
  using stagecore::stagelaser::Limits;
  using stagecore::stagelaser::limits_valid;

  Limits defaults;
  assert(limits_valid(defaults));

  Limits invalid = defaults;
  invalid.pulse_ms = 0;
  assert(!limits_valid(invalid));

  invalid = defaults;
  invalid.pulse_ms = 10001;
  assert(!limits_valid(invalid));

  invalid = defaults;
  invalid.min_rest_ms = 60001;
  assert(!limits_valid(invalid));

  invalid = defaults;
  invalid.min_flash_hz = 2.0;
  invalid.max_flash_hz = 1.0;
  assert(!limits_valid(invalid));

  invalid = defaults;
  invalid.min_flash_hz = std::numeric_limits<double>::quiet_NaN();
  assert(!limits_valid(invalid));

  invalid = defaults;
  invalid.max_flash_duration_ms = 0;
  assert(!limits_valid(invalid));

  invalid = defaults;
  invalid.max_flash_duration_ms = 3600001;
  assert(!limits_valid(invalid));

  return 0;
}
