#pragma once

#include "config_store.h"
#include "device_identity.h"
#include "esp_err.h"
#include "hub_discovery.h"
#include "hub_security.h"
#include "laser_controller.h"

namespace stagecore {

esp_err_t run_stage_device_runtime(
    const VerifiedHub &hub,
    const RuntimeCredential &credential,
    const DeviceIdentity &identity,
    const DeviceConfig &config,
    stagelaser::LaserController *laser);

}  // namespace stagecore
