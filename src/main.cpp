#include <string>

#include "config_store.h"
#include "device_identity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hub_discovery.h"
#include "hub_security.h"
#include "laser_controller.h"
#include "laser_controller_esp.h"
#include "laser_state_store.h"
#include "network_station.h"
#include "nvs_flash.h"
#include "provisioning.h"
#include "relay_output.h"
#include "stage_device_runtime.h"
#include "esp_system.h"

#ifndef STAGECORE_FW_VERSION
#define STAGECORE_FW_VERSION "0.1.0-dev.1"
#endif
#ifndef STAGECORE_BUILD_REVISION
#define STAGECORE_BUILD_REVISION "unknown"
#endif
#ifndef STAGECORE_LASER_SHARED_POWER_QUALIFIED
#define STAGECORE_LASER_SHARED_POWER_QUALIFIED 0
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
  while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

std::string default_display_name(const std::string &device_id) {
  if (device_id.size() >= 6) {
    return "StageLaser-" + device_id.substr(device_id.size() - 6);
  }
  return "StageLaser";
}

stagecore::stagelaser::ResetClass classify_reset_reason(esp_reset_reason_t reason) {
  using stagecore::stagelaser::ResetClass;
  switch (reason) {
    case ESP_RST_SW:
      return ResetClass::kSoftware;
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:
      return ResetClass::kWatchdog;
    case ESP_RST_POWERON:
      return ResetClass::kPowerOn;
    case ESP_RST_BROWNOUT:
      return ResetClass::kBrownout;
    default:
      return ResetClass::kUnknown;
  }
}
}  // namespace

extern "C" void app_main(void) {
  init_nvs();
  ESP_LOGI(kTag, "StageLaser firmware %s (%s)", STAGECORE_FW_VERSION,
           STAGECORE_BUILD_REVISION);

  ESP_ERROR_CHECK(stagecore::stagelaser::relay_output_init());

  stagecore::stagelaser::PersistentState persisted;
  bool persisted_found = false;
  if (stagecore::stagelaser::load_persistent_state(
          &persisted, &persisted_found) != ESP_OK) {
    hold_safe_failure("laser state storage unavailable");
  }

  const esp_reset_reason_t boot_reset_reason = esp_reset_reason();
  const bool shared_power_qualified =
      STAGECORE_LASER_SHARED_POWER_QUALIFIED == 1;

  stagecore::stagelaser::NvsPersistentStateSink laser_store;
  stagecore::stagelaser::RelayOutputActuator laser_actuator;
  stagecore::stagelaser::LaserController laser(
      persisted, classify_reset_reason(boot_reset_reason),
      shared_power_qualified, &laser_store, &laser_actuator);
  if (!laser_store.Save(laser.machine().PersistentSnapshot())) {
    hold_safe_failure("unable to persist restored laser truth state");
  }
  ESP_LOGI(kTag,
           "laser truth restored; persisted=%s reset_reason=%d "
           "shared_power_qualified=%s resync_required=%s",
           persisted_found ? "yes" : "no",
           static_cast<int>(boot_reset_reason),
           shared_power_qualified ? "yes" : "no",
           laser.machine().resync_required() ? "yes" : "no");

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
    stagecore::run_provisioning_portal(
        identity.device_id(), default_display_name(identity.device_id()));
  }

  esp_err_t network =
      stagecore::connect_station(config.wifi_ssid, config.wifi_password, 30000);
  while (network != ESP_OK) {
    ESP_LOGW(kTag, "waiting for Stage LAN: %s", esp_err_to_name(network));
    network = stagecore::wait_for_station_connection(30000);
  }
  ESP_LOGI(kTag, "Stage LAN connected as %s", config.display_name.c_str());

  while (true) {
    if (stagecore::wait_for_station_connection(0) != ESP_OK) {
      ESP_LOGW(kTag, "Stage LAN disconnected; waiting for reconnect");
      if (stagecore::wait_for_station_connection(30000) != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }
    }

    stagecore::VerifiedHub hub;
    if (stagecore::discover_and_verify_hub(&hub) != ESP_OK) {
      ESP_LOGW(kTag, "verified StageCore Hub not available yet");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    ESP_LOGI(kTag, "verified Hub %s at %s:%u", hub.hub_id.c_str(),
             hub.address.c_str(), hub.port);

    stagecore::RuntimeCredential credential;
    if (stagecore::ensure_paired_and_authenticate(
            hub, &identity, config.display_name, &credential) != ESP_OK) {
      ESP_LOGW(kTag, "Hub pairing/authentication not ready; retrying");
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }

    ESP_LOGI(kTag,
             "authenticated StageCore v2 runtime starting");
    const esp_err_t runtime_err =
        stagecore::run_stage_device_runtime(
            hub, credential, identity, config, &laser);

    // The Hub owns ACTIVE scope and command authority. Runtime exit clears the
    // credential and the runtime itself attempts deterministic Safe Off only
    // from known state; UNKNOWN is never blindly toggled.
    credential = stagecore::RuntimeCredential{};
    ESP_LOGW(kTag, "Stage Device runtime ended: %s",
             esp_err_to_name(runtime_err));
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
