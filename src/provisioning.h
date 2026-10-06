#pragma once

#include <string>

#include "esp_err.h"

namespace stagecore {

[[noreturn]] void run_provisioning_portal(
    const std::string &device_id,
    const std::string &default_display_name);

}  // namespace stagecore
