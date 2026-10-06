#pragma once

#include "esp_err.h"

namespace stagecore::stagelaser {

// Hardware output is intentionally compile-time gated. The bootstrap image is
// NO-ACTUATION and must not touch a GPIO. A qualified board environment must
// explicitly set STAGECORE_LASER_ACTUATION_ENABLED=1 and a reviewed GPIO.
esp_err_t relay_output_init();
esp_err_t relay_pick();
esp_err_t relay_release();
bool relay_actuation_enabled();

}  // namespace stagecore::stagelaser
