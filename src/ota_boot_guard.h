#pragma once

#include <string>

#include "esp_err.h"

namespace stagecore::stagelaser {

// Persist the exact update identity before selecting a new OTA boot partition.
// A pending rollback-protected image is never confirmed unless its compiled
// firmware version and source revision match this expectation.
esp_err_t ota_record_expected_boot(
    const std::string &update_id,
    const std::string &target_version,
    const std::string &source_revision);

esp_err_t ota_clear_expected_boot();

// Confirm a pending OTA image only after app_main has completed the local
// StageLaser safety checkpoint AND the running image identity matches the
// persisted target version + source revision. Non-OTA/default builds are a no-op.
esp_err_t ota_confirm_safe_boot_if_pending();

}  // namespace stagecore::stagelaser
