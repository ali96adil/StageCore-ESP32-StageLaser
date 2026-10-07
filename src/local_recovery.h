#pragma once

#include "esp_err.h"
#include "laser_controller.h"

namespace stagecore::stagelaser {

// Boot-time physical-presence recovery.
//
// The function is a no-op while no recovery GPIO is qualified. Once a GPIO is
// explicitly qualified, a continuous 10-second active-low hold may clear only
// remembered Hub trust. It never drives the relay or changes laser state, and
// it refuses the trust reset unless the controller already reports a known,
// DISARMED, stable OFF state.
esp_err_t maybe_run_boot_hub_trust_reset(const LaserController &laser);

}  // namespace stagecore::stagelaser
