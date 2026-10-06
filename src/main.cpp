#include "config_store.h"
#include "device_identity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "laser_state_machine.h"
#include "nvs_flash.h"
#include "relay_output.h"

#ifndef STAGECORE_FW_VERSION
#define STAGECORE_FW_VERSION "0.1.0-dev"
#endif
#ifndef STAGECORE_BUILD_REVISION
#define STAGECORE_BUILD_REVISION "unknown"
#endif

namespace {
constexpr char kTag[] = "stagelaser";

void init_nvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

[[noreturn]] void hold_safe_failure(const char *reason) {
  ESP_LOGE(kTag, "safe failure: %s", reason);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
}  // namespace

extern "C" void app_main(void) {
  init_nvs();
  ESP_LOGI(kTag, "StageLaser firmware %s (%s)", STAGECORE_FW_VERSION,
           STAGECORE_BUILD_REVISION);

  ESP_ERROR_CHECK(stagecore::stagelaser::relay_output_init());

  stagecore::DeviceIdentity identity;
  if (identity.LoadOrCreate() != ESP_OK) {
    hold_safe_failure("persistent P-256 identity unavailable");
  }
  ESP_LOGI(kTag, "device_id=%s", identity.device_id().c_str());

  stagecore::DeviceConfig config;
  if (stagecore::load_device_config(&config) != ESP_OK) {
    hold_safe_failure("configuration storage unavailable");
  }
  if (!config.complete()) {
    ESP_LOGW(kTag,
             "configuration incomplete; provisioning slice not installed yet; "
             "relay remains NO-ACTUATION");
  } else {
    ESP_LOGI(kTag, "stored Stage LAN configuration available for %s",
             config.display_name.c_str());
  }

  ESP_LOGW(kTag,
           "identity/config slice only; networking/runtime still disabled; "
           "relay remains NO-ACTUATION");
}
