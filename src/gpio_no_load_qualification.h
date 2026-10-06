#pragma once

#include "esp_err.h"

namespace stagecore::stagelaser {

// Qualification-only helper. The normal StageLaser environments leave this
// disabled and never touch the candidate GPIO.
esp_err_t gpio_no_load_qualification_init();
bool gpio_no_load_qualification_enabled();

}  // namespace stagecore::stagelaser
