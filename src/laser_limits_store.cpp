#include "laser_limits_store.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include "laser_limits.h"
#include "nvs.h"

namespace stagecore::stagelaser {
namespace {

constexpr char kNamespace[] = "laser_limits";
constexpr char kVersionKey[] = "version";
constexpr char kPulseKey[] = "pulse_ms";
constexpr char kRestKey[] = "rest_ms";
constexpr char kMinFlashMilliHzKey[] = "min_mhz";
constexpr char kMaxFlashMilliHzKey[] = "max_mhz";
constexpr char kDurationKey[] = "duration_ms";
constexpr uint8_t kSchemaVersion = 1;

bool hz_to_millihz(double hz, uint32_t *out) {
  if (out == nullptr || !std::isfinite(hz) || hz <= 0.0) return false;
  const double scaled = hz * 1000.0;
  if (scaled < 1.0 ||
      scaled > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
    return false;
  }
  *out = static_cast<uint32_t>(std::llround(scaled));
  return *out != 0;
}

}  // namespace

esp_err_t load_persistent_limits(Limits *limits, bool *found) {
  if (limits == nullptr || found == nullptr) return ESP_ERR_INVALID_ARG;
  *limits = Limits{};
  *found = false;

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;

  uint8_t version = 0;
  err = nvs_get_u8(handle, kVersionKey, &version);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return ESP_OK;
  }
  if (err != ESP_OK || version != kSchemaVersion) {
    nvs_close(handle);
    return err == ESP_OK ? ESP_ERR_INVALID_VERSION : err;
  }

  uint32_t pulse_ms = 0;
  uint32_t rest_ms = 0;
  uint32_t min_millihz = 0;
  uint32_t max_millihz = 0;
  uint32_t duration_ms = 0;

  if ((err = nvs_get_u32(handle, kPulseKey, &pulse_ms)) == ESP_OK &&
      (err = nvs_get_u32(handle, kRestKey, &rest_ms)) == ESP_OK &&
      (err = nvs_get_u32(handle, kMinFlashMilliHzKey, &min_millihz)) == ESP_OK &&
      (err = nvs_get_u32(handle, kMaxFlashMilliHzKey, &max_millihz)) == ESP_OK &&
      (err = nvs_get_u32(handle, kDurationKey, &duration_ms)) == ESP_OK) {
    Limits loaded;
    loaded.pulse_ms = pulse_ms;
    loaded.min_rest_ms = rest_ms;
    loaded.min_flash_hz = static_cast<double>(min_millihz) / 1000.0;
    loaded.max_flash_hz = static_cast<double>(max_millihz) / 1000.0;
    loaded.max_flash_duration_ms = duration_ms;

    if (!limits_valid(loaded)) {
      nvs_close(handle);
      return ESP_ERR_INVALID_STATE;
    }

    *limits = loaded;
    *found = true;
  }

  nvs_close(handle);
  return err;
}

esp_err_t save_persistent_limits(const Limits &limits) {
  if (!limits_valid(limits)) return ESP_ERR_INVALID_ARG;

  uint32_t min_millihz = 0;
  uint32_t max_millihz = 0;
  if (!hz_to_millihz(limits.min_flash_hz, &min_millihz) ||
      !hz_to_millihz(limits.max_flash_hz, &max_millihz)) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;

  if ((err = nvs_set_u8(handle, kVersionKey, kSchemaVersion)) == ESP_OK &&
      (err = nvs_set_u32(handle, kPulseKey, limits.pulse_ms)) == ESP_OK &&
      (err = nvs_set_u32(handle, kRestKey, limits.min_rest_ms)) == ESP_OK &&
      (err = nvs_set_u32(handle, kMinFlashMilliHzKey, min_millihz)) == ESP_OK &&
      (err = nvs_set_u32(handle, kMaxFlashMilliHzKey, max_millihz)) == ESP_OK &&
      (err = nvs_set_u32(handle, kDurationKey,
                         limits.max_flash_duration_ms)) == ESP_OK) {
    err = nvs_commit(handle);
  }

  nvs_close(handle);
  return err;
}

esp_err_t clear_persistent_limits() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;
  err = nvs_erase_all(handle);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

}  // namespace stagecore::stagelaser
