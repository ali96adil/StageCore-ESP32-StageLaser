#pragma once

#include "esp_err.h"

namespace stagecore::stagelaser {

// Confirm a pending OTA image only after app_main has completed the local
// StageLaser safety checkpoint. Non-OTA/default builds are a no-op.
esp_err_t ota_confirm_safe_boot_if_pending();

}  // namespace stagecore::stagelaser
