#include <string>

#include "config_store.h"
#include "device_identity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_no_load_qualification.h"
#include "hub_discovery.h"
#include "hub_security.h"
#include "laser_controller.h"
#include "laser_controller_esp.h"
#include "laser_limits_store.h"
#include "laser_state_store.h"
#include "local_recovery.h"
#include "network_station.h"
#include "ota_boot_guard.h"
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
#ifndef STAGECORE_OTA_ENABLED
#define STAGECORE_OTA_ENABLED 0
#endif
#ifndef STAGECORE_LASER_ACTUATION_ENABLED
#define STAGECORE_LASER_ACTUATION_ENABLED 0
#endif

namespace {
constexpr char kTag[] = "stagelaser";

esp_err_t init_nvs() {
  // StageLaser persistence contains device identity, Hub trust, physical truth
  // and replay fences. Never erase it automatically on an initialization
  // error. Recovery/clear operations must be explicit and attended.
  return nvs_flash_init();
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

stagecore::FoundationDeviceDescriptor foundation_descriptor() {
  stagecore::FoundationDeviceDescriptor descriptor;
  descriptor.hostname_prefix = "stagecore-laser-";
  descriptor.platform = "esp32";
  descriptor.architecture = "riscv32";
  descriptor.firmware_version = STAGECORE_FW_VERSION;
  descriptor.capabilities = {
      "laser.arm",
      "laser.disarm",
      "laser.state.set",
      "laser.flash.start",
      "laser.flash.stop",
      "laser.safe_off",
      "laser.state.read",
      "laser.state.resync",
  };
#if STAGECORE_OTA_ENABLED == 1
  descriptor.capabilities.push_back("device.maintenance.firmware-update");
#endif
  return descriptor;
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
  ESP_ERROR_CHECK(stagecore::stagelaser::gpio_no_load_qualification_init());
  const esp_err_t nvs_err = init_nvs();
  if (nvs_err != ESP_OK) {
    ESP_LOGE(kTag,
             "NVS init failed: %s; automatic erase is prohibited",
             esp_err_to_name(nvs_err));
    hold_safe_failure("persistent safety state unavailable");
  }
  ESP_LOGI(kTag, "StageLaser firmware %s (%s)", STAGECORE_FW_VERSION,
           STAGECORE_BUILD_REVISION);

  ESP_ERROR_CHECK(stagecore::stagelaser::relay_output_init());

  stagecore::stagelaser::PersistentState persisted;
  bool persisted_found = false;
  if (stagecore::stagelaser::load_persistent_state(
          &persisted, &persisted_found) != ESP_OK) {
    hold_safe_failure("laser state storage unavailable");
  }

  stagecore::stagelaser::Limits limits;
  bool limits_found = false;
  if (stagecore::stagelaser::load_persistent_limits(
          &limits, &limits_found) != ESP_OK) {
    hold_safe_failure("laser timing limits unavailable or invalid");
  }
  if (!limits_found &&
      stagecore::stagelaser::save_persistent_limits(limits) != ESP_OK) {
    hold_safe_failure("unable to persist default laser timing limits");
  }
  ESP_LOGI(kTag,
           "laser limits restored; persisted=%s pulse_ms=%u min_rest_ms=%u "
           "min_flash_hz=%.3f max_flash_hz=%.3f max_duration_ms=%u",
           limits_found ? "yes" : "no",
           static_cast<unsigned>(limits.pulse_ms),
           static_cast<unsigned>(limits.min_rest_ms),
           limits.min_flash_hz,
           limits.max_flash_hz,
           static_cast<unsigned>(limits.max_flash_duration_ms));

  const esp_reset_reason_t boot_reset_reason = esp_reset_reason();
  const bool shared_power_qualified =
      STAGECORE_LASER_SHARED_POWER_QUALIFIED == 1;

  stagecore::stagelaser::NvsPersistentStateSink laser_store;
  stagecore::stagelaser::RelayOutputActuator laser_actuator;
  stagecore::stagelaser::LaserController laser(
      persisted, classify_reset_reason(boot_reset_reason),
      shared_power_qualified, &laser_store, &laser_actuator, limits);
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
  stagecore::FoundationStore &foundation = stagecore::foundation_store();
  const stagecore::FoundationDeviceDescriptor descriptor =
      foundation_descriptor();
  ESP_LOGI(kTag, "device_id=%s", identity.device_id().c_str());

  stagecore::DeviceConfig config;
  if (stagecore::load_device_config(&config) != ESP_OK) {
    hold_safe_failure("configuration storage unavailable");
  }

  // Local recovery is deliberately boot-time and non-actuating. With no
  // physically qualified recovery GPIO configured, this is a no-op.
  if (stagecore::stagelaser::maybe_run_boot_hub_trust_reset(laser) != ESP_OK) {
    hold_safe_failure("local Hub trust recovery refused or failed");
  }

  // An OTA image is confirmed only after all local safety-critical persistent
  // state, timing limits, relay initialization, identity and configuration
  // storage have been restored successfully. Network availability is not part
  // of this checkpoint, so a safe image can still boot into provisioning.
  if (stagecore::stagelaser::ota_confirm_safe_boot_if_pending() != ESP_OK) {
    hold_safe_failure("OTA candidate safe-boot confirmation failed");
  }
  // Native USB-Serial/JTAG re-enumerates on reset. On the qualified C3 board,
  // the host can reconnect after the earliest app logs have already been
  // emitted. Repeat a compact, non-secret qualification summary here after
  // persistent safety state, identity and config storage are all restored.
  ESP_LOGI(kTag, "StageLaser firmware %s (%s)", STAGECORE_FW_VERSION,
           STAGECORE_BUILD_REVISION);
#if STAGECORE_LASER_ACTUATION_ENABLED == 0
  ESP_LOGW(kTag, "NO-ACTUATION build: relay GPIO is intentionally disabled");
#endif
  ESP_LOGI(kTag,
           "laser truth restored; persisted=%s reset_reason=%d "
           "shared_power_qualified=%s resync_required=%s",
           persisted_found ? "yes" : "no",
           static_cast<int>(boot_reset_reason),
           shared_power_qualified ? "yes" : "no",
           laser.machine().resync_required() ? "yes" : "no");
  ESP_LOGI(kTag, "device_id=%s", identity.device_id().c_str());

  if (!config.complete()) {
    stagecore::run_provisioning_portal(
        identity.device_id(), default_display_name(identity.device_id()));
  }

  constexpr int kRecoveryAfterFailed30sWindows = 3;
  int failed_network_windows = 0;
  esp_err_t network =
      stagecore::connect_station(config.wifi_ssid, config.wifi_password, 30000);
  while (network != ESP_OK) {
    ++failed_network_windows;
    ESP_LOGW(kTag, "waiting for Stage LAN (%d/%d): %s",
             failed_network_windows, kRecoveryAfterFailed30sWindows,
             esp_err_to_name(network));
    if (failed_network_windows >= kRecoveryAfterFailed30sWindows) {
      const esp_err_t recovery = stagecore::run_recovery_portal(
          identity.device_id(), config.display_name);
      if (recovery != ESP_OK) {
        hold_safe_failure("Stage LAN recovery portal failed");
      }
      failed_network_windows = 0;
      network = stagecore::wait_for_station_connection(0);
      continue;
    }
    network = stagecore::wait_for_station_connection(30000);
  }
  failed_network_windows = 0;
  ESP_LOGI(kTag, "Stage LAN connected as %s", config.display_name.c_str());

  while (true) {
    if (stagecore::wait_for_station_connection(0) != ESP_OK) {
      ESP_LOGW(kTag, "Stage LAN disconnected; waiting for reconnect");
      if (stagecore::wait_for_station_connection(30000) != ESP_OK) {
        ++failed_network_windows;
        if (failed_network_windows >= kRecoveryAfterFailed30sWindows) {
          const esp_err_t recovery = stagecore::run_recovery_portal(
              identity.device_id(), config.display_name);
          if (recovery != ESP_OK) {
            hold_safe_failure("Stage LAN recovery portal failed");
          }
          failed_network_windows = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }
    }
    failed_network_windows = 0;

    stagecore::VerifiedHub hub;
    if (stagecore::discover_and_verify_hub(&foundation, &hub) != ESP_OK) {
      ESP_LOGW(kTag, "verified StageCore Hub not available yet");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    ESP_LOGI(kTag, "verified Hub %s at %s:%u", hub.hub_id.c_str(),
             hub.address.c_str(), hub.port);

    stagecore::RuntimeCredential credential;
    if (stagecore::ensure_paired_and_authenticate(
            hub, &identity, descriptor, config.display_name, &credential) !=
        ESP_OK) {
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
