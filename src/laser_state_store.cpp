#include "laser_state_store.h"

#include <cstdint>

#include "nvs.h"

namespace stagecore::stagelaser {
namespace {

constexpr char kNamespace[] = "laser_state";
constexpr char kVersionKey[] = "version";
constexpr char kStableKey[] = "stable";
constexpr char kQualityKey[] = "quality";
constexpr char kTransitionKey[] = "transition";
constexpr char kFlashKey[] = "flash";
constexpr char kPulseCountKey[] = "pulse_count";
constexpr uint8_t kSchemaVersion = 1;

uint8_t stable_wire(LogicalState state) {
  switch (state) {
    case LogicalState::kOff: return 1;
    case LogicalState::kOn: return 2;
    default: return 0;
  }
}

LogicalState stable_from_wire(uint8_t value) {
  switch (value) {
    case 1: return LogicalState::kOff;
    case 2: return LogicalState::kOn;
    default: return LogicalState::kUnknown;
  }
}

uint8_t quality_wire(StateQuality quality) {
  switch (quality) {
    case StateQuality::kTracked: return 1;
    case StateQuality::kConfirmed: return 2;
    default: return 0;
  }
}

StateQuality quality_from_wire(uint8_t value) {
  switch (value) {
    case 1: return StateQuality::kTracked;
    case 2: return StateQuality::kConfirmed;
    default: return StateQuality::kUnknown;
  }
}

}  // namespace

esp_err_t load_persistent_state(PersistentState *state, bool *found) {
  if (state == nullptr || found == nullptr) return ESP_ERR_INVALID_ARG;
  *state = PersistentState{};
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

  uint8_t stable = 0;
  uint8_t quality = 0;
  uint8_t transition = 0;
  uint8_t flash = 0;
  uint64_t pulse_count = 0;
  if ((err = nvs_get_u8(handle, kStableKey, &stable)) == ESP_OK &&
      (err = nvs_get_u8(handle, kQualityKey, &quality)) == ESP_OK &&
      (err = nvs_get_u8(handle, kTransitionKey, &transition)) == ESP_OK &&
      (err = nvs_get_u8(handle, kFlashKey, &flash)) == ESP_OK &&
      (err = nvs_get_u64(handle, kPulseCountKey, &pulse_count)) == ESP_OK) {
    state->stable_state = stable_from_wire(stable);
    state->quality = quality_from_wire(quality);
    state->interrupted_transition = transition != 0;
    state->flash_session_in_progress = flash != 0;
    state->relay_pulse_count = pulse_count;
    *found = true;
  }
  nvs_close(handle);
  return err;
}

esp_err_t save_persistent_state(const PersistentState &state) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;

  if ((err = nvs_set_u8(handle, kVersionKey, kSchemaVersion)) == ESP_OK &&
      (err = nvs_set_u8(handle, kStableKey, stable_wire(state.stable_state))) == ESP_OK &&
      (err = nvs_set_u8(handle, kQualityKey, quality_wire(state.quality))) == ESP_OK &&
      (err = nvs_set_u8(handle, kTransitionKey,
                        state.interrupted_transition ? 1 : 0)) == ESP_OK &&
      (err = nvs_set_u8(handle, kFlashKey,
                        state.flash_session_in_progress ? 1 : 0)) == ESP_OK &&
      (err = nvs_set_u64(handle, kPulseCountKey,
                         state.relay_pulse_count)) == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}

esp_err_t clear_persistent_state() {
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
