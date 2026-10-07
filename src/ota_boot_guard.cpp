#include "ota_boot_guard.h"

#include <array>
#include <string>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs.h"
#include "sdkconfig.h"

#ifndef STAGECORE_FW_VERSION
#define STAGECORE_FW_VERSION "unknown"
#endif
#ifndef STAGECORE_BUILD_REVISION
#define STAGECORE_BUILD_REVISION "unknown"
#endif

namespace stagecore::stagelaser {
namespace {

constexpr char kTag[] = "stagelaser-ota";
constexpr char kNamespace[] = "stg_ota";
constexpr char kUpdateIDKey[] = "update_id";
constexpr char kTargetVersionKey[] = "target_ver";
constexpr char kSourceRevisionKey[] = "source_rev";

struct BootExpectation {
  bool found = false;
  std::string update_id;
  std::string target_version;
  std::string source_revision;
};

esp_err_t read_string(nvs_handle_t handle,
                      const char *key,
                      char *buffer,
                      size_t buffer_size,
                      std::string *out) {
  if (buffer == nullptr || out == nullptr || buffer_size == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  size_t required = buffer_size;
  const esp_err_t err = nvs_get_str(handle, key, buffer, &required);
  if (err != ESP_OK) return err;
  if (required == 0 || required > buffer_size) return ESP_ERR_INVALID_SIZE;
  *out = buffer;
  return ESP_OK;
}

esp_err_t load_expectation(BootExpectation *expectation) {
  if (expectation == nullptr) return ESP_ERR_INVALID_ARG;
  *expectation = BootExpectation{};

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;

  std::array<char, 37> update_id{};
  std::array<char, 129> target_version{};
  std::array<char, 41> source_revision{};

  err = read_string(
      handle, kUpdateIDKey, update_id.data(), update_id.size(),
      &expectation->update_id);
  if (err == ESP_OK) {
    err = read_string(
        handle, kTargetVersionKey,
        target_version.data(), target_version.size(),
        &expectation->target_version);
  }
  if (err == ESP_OK) {
    err = read_string(
        handle, kSourceRevisionKey,
        source_revision.data(), source_revision.size(),
        &expectation->source_revision);
  }
  nvs_close(handle);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    *expectation = BootExpectation{};
    return ESP_OK;
  }
  if (err != ESP_OK) return err;

  expectation->found =
      expectation->update_id.size() == 36 &&
      !expectation->target_version.empty() &&
      expectation->target_version.size() <= 128 &&
      expectation->source_revision.size() == 40;
  if (!expectation->found) return ESP_ERR_INVALID_STATE;
  return ESP_OK;
}

esp_err_t rollback_pending_image(const char *reason) {
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) &&     CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  ESP_LOGE(kTag, "rejecting pending OTA image: %s", reason);
  const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "automatic OTA rollback failed: %s",
             esp_err_to_name(err));
  }
  return err;
#else
  (void)reason;
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

}  // namespace

esp_err_t ota_record_expected_boot(
    const std::string &update_id,
    const std::string &target_version,
    const std::string &source_revision) {
  if (update_id.size() != 36 ||
      target_version.empty() || target_version.size() > 128 ||
      source_revision.size() != 40) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;

  err = nvs_set_str(handle, kUpdateIDKey, update_id.c_str());
  if (err == ESP_OK) {
    err = nvs_set_str(handle, kTargetVersionKey, target_version.c_str());
  }
  if (err == ESP_OK) {
    err = nvs_set_str(handle, kSourceRevisionKey, source_revision.c_str());
  }
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);

  if (err == ESP_OK) {
    ESP_LOGI(kTag,
             "persisted expected OTA identity update=%s target=%s rev=%s",
             update_id.c_str(), target_version.c_str(),
             source_revision.c_str());
  }
  return err;
}

esp_err_t ota_clear_expected_boot() {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;
  err = nvs_erase_all(handle);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t ota_confirm_safe_boot_if_pending() {
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) &&     CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running == nullptr) {
    ESP_LOGE(kTag, "running OTA partition is unavailable");
    return ESP_FAIL;
  }

  BootExpectation expectation;
  const esp_err_t expectation_err = load_expectation(&expectation);
  if (expectation_err != ESP_OK) {
    ESP_LOGE(kTag, "unable to read expected OTA identity: %s",
             esp_err_to_name(expectation_err));
  }

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t state_err = esp_ota_get_state_partition(running, &state);
  if (state_err == ESP_ERR_NOT_FOUND) {
    // A serial-flashed qualification image can legitimately have no otadata
    // record yet. Clear any stale expectation left by an earlier rollback.
    if (expectation.found) {
      const esp_err_t clear_err = ota_clear_expected_boot();
      if (clear_err != ESP_OK) {
        ESP_LOGW(kTag, "unable to clear stale OTA expectation: %s",
                 esp_err_to_name(clear_err));
      }
    }
    ESP_LOGI(kTag, "no OTA state record; safe-boot confirmation not required");
    return ESP_OK;
  }
  if (state_err != ESP_OK) {
    ESP_LOGE(kTag, "unable to read running OTA state: %s",
             esp_err_to_name(state_err));
    return state_err;
  }

  if (state != ESP_OTA_IMG_PENDING_VERIFY) {
    // A successful rollback returns to an older valid image while the
    // expectation still names the rejected target. It is no longer needed.
    if (expectation.found) {
      const esp_err_t clear_err = ota_clear_expected_boot();
      if (clear_err != ESP_OK) {
        ESP_LOGW(kTag, "unable to clear completed/stale OTA expectation: %s",
                 esp_err_to_name(clear_err));
      }
    }
    ESP_LOGI(kTag, "running OTA image state=%d; confirmation not required",
             static_cast<int>(state));
    return ESP_OK;
  }

  if (expectation_err != ESP_OK || !expectation.found) {
    return rollback_pending_image(
        "pending image has no trustworthy persisted update identity");
  }
  if (expectation.target_version != STAGECORE_FW_VERSION) {
    return rollback_pending_image(
        "compiled firmware version does not match the qualified target");
  }
  if (expectation.source_revision != STAGECORE_BUILD_REVISION) {
    return rollback_pending_image(
        "compiled source revision does not match the qualified artifact");
  }

  const esp_err_t confirm_err = esp_ota_mark_app_valid_cancel_rollback();
  if (confirm_err != ESP_OK) {
    ESP_LOGE(kTag, "unable to confirm OTA image after safe boot: %s",
             esp_err_to_name(confirm_err));
    return confirm_err;
  }

  const esp_err_t clear_err = ota_clear_expected_boot();
  if (clear_err != ESP_OK) {
    // The image is already confirmed. A stale expectation is harmless and will
    // be retried on the next boot; do not turn a valid image into a boot loop.
    ESP_LOGW(kTag, "OTA image confirmed but expectation cleanup failed: %s",
             esp_err_to_name(clear_err));
  }

  ESP_LOGI(kTag,
           "pending OTA image confirmed after local safe boot and exact "
           "version/revision identity match");
  return ESP_OK;
#else
  return ESP_OK;
#endif
}

}  // namespace stagecore::stagelaser
