#include "ota_boot_guard.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "sdkconfig.h"

namespace stagecore::stagelaser {
namespace {

constexpr char kTag[] = "stagelaser-ota";

}  // namespace

esp_err_t ota_confirm_safe_boot_if_pending() {
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) &&     CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running == nullptr) {
    ESP_LOGE(kTag, "running OTA partition is unavailable");
    return ESP_FAIL;
  }

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t state_err = esp_ota_get_state_partition(running, &state);
  if (state_err == ESP_ERR_NOT_FOUND) {
    // A serial-flashed qualification image can legitimately have no otadata
    // record yet. There is nothing to confirm in that case.
    ESP_LOGI(kTag, "no OTA state record; safe-boot confirmation not required");
    return ESP_OK;
  }
  if (state_err != ESP_OK) {
    ESP_LOGE(kTag, "unable to read running OTA state: %s",
             esp_err_to_name(state_err));
    return state_err;
  }

  if (state != ESP_OTA_IMG_PENDING_VERIFY) {
    ESP_LOGI(kTag, "running OTA image state=%d; confirmation not required",
             static_cast<int>(state));
    return ESP_OK;
  }

  const esp_err_t confirm_err = esp_ota_mark_app_valid_cancel_rollback();
  if (confirm_err != ESP_OK) {
    ESP_LOGE(kTag, "unable to confirm OTA image after safe boot: %s",
             esp_err_to_name(confirm_err));
    return confirm_err;
  }

  ESP_LOGI(kTag,
           "pending OTA image confirmed after local StageLaser safe-boot "
           "checkpoint");
  return ESP_OK;
#else
  return ESP_OK;
#endif
}

}  // namespace stagecore::stagelaser
