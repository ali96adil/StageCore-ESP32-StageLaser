#include <string>

#include "config_store.h"
#include "device_identity.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hub_discovery.h"
#include "hub_security.h"
#include "network_station.h"
#include "nvs_flash.h"
#include "provisioning.h"
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
  while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

std::string default_display_name(const std::string &device_id) {
  if (device_id.size() >= 6) {
    return "StageLaser-" + device_id.substr(device_id.size() - 6);
  }
  return "StageLaser";
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

  stagecore::VerifiedHub hub;
  while (stagecore::discover_and_verify_hub(&hub) != ESP_OK) {
    ESP_LOGW(kTag, "verified StageCore Hub not available yet");
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
  ESP_LOGI(kTag, "verified Hub %s at %s:%u", hub.hub_id.c_str(),
           hub.address.c_str(), hub.port);

  stagecore::RuntimeCredential credential;
  while (stagecore::ensure_paired_and_authenticate(
             hub, &identity, config.display_name, &credential) != ESP_OK) {
    ESP_LOGW(kTag, "Hub pairing/authentication not ready; retrying");
    vTaskDelay(pdMS_TO_TICKS(3000));
  }

  ESP_LOGI(kTag, "authenticated StageCore runtime credential acquired");
  ESP_LOGW(kTag,
           "Hub trust slice complete; Stage Device command runtime is not "
           "installed yet; relay remains NO-ACTUATION");

  while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}
