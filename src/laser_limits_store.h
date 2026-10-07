#pragma once

#include "esp_err.h"
#include "laser_contract.h"

namespace stagecore::stagelaser {

// Load the qualified output timing limits. Missing storage is not an error:
// defaults are returned with found=false so first boot can persist them.
// Invalid/corrupt stored limits fail closed instead of silently restoring
// potentially unsafe timing.
esp_err_t load_persistent_limits(Limits *limits, bool *found);
esp_err_t save_persistent_limits(const Limits &limits);
esp_err_t clear_persistent_limits();

}  // namespace stagecore::stagelaser
