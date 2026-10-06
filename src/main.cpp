#include "esp_log.h"
#include "laser_state_machine.h"
#include "relay_output.h"

#ifndef STAGECORE_FW_VERSION
#define STAGECORE_FW_VERSION "0.1.0-dev"
#endif
#ifndef STAGECORE_BUILD_REVISION
#define STAGECORE_BUILD_REVISION "unknown"
#endif

namespace {
constexpr char kTag[] = "stagelaser";
}

extern "C" void app_main(void) {
  ESP_LOGI(kTag, "StageLaser firmware bootstrap %s (%s)", STAGECORE_FW_VERSION,
           STAGECORE_BUILD_REVISION);
  ESP_ERROR_CHECK(stagecore::stagelaser::relay_output_init());
  ESP_LOGW(kTag,
           "bootstrap only: Stage Device v2 transport/provisioning will be added "
           "after the dedicated firmware repository is created; actuation remains disabled");
}
