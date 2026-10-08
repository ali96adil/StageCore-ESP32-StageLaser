#include "local_recovery.h"

#include <cstdint>

#include "config_store.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "recovery_policy.h"

#ifndef STAGECORE_LASER_LOCAL_RECOVERY_GPIO
#define STAGECORE_LASER_LOCAL_RECOVERY_GPIO -1
#endif

#ifndef STAGECORE_LASER_LOCAL_RECOVERY_QUALIFIED
#define STAGECORE_LASER_LOCAL_RECOVERY_QUALIFIED 0
#endif

namespace stagecore::stagelaser {
namespace {

constexpr char kTag[] = "stagelaser-recovery";
constexpr uint32_t kTrustResetHoldMs = 10000;
constexpr uint32_t kPollMs = 50;

static_assert(
    (STAGECORE_LASER_LOCAL_RECOVERY_GPIO == -1 &&
     STAGECORE_LASER_LOCAL_RECOVERY_QUALIFIED == 0) ||
        (STAGECORE_LASER_LOCAL_RECOVERY_GPIO >= 0 &&
         STAGECORE_LASER_LOCAL_RECOVERY_GPIO <= 21 &&
         STAGECORE_LASER_LOCAL_RECOVERY_QUALIFIED == 1),
    "StageLaser local recovery requires an explicitly qualified ESP32-C3 GPIO");

bool known_safe_off(const LaserController &laser) {
  const auto &machine = laser.machine();
  const bool quality_known =
      machine.state_quality() == StateQuality::kTracked ||
      machine.state_quality() == StateQuality::kConfirmed;

  return machine.arm_state() == ArmState::kDisarmed &&
         machine.logical_state() == LogicalState::kOff &&
         quality_known &&
         !machine.resync_required() &&
         !machine.pulse_in_progress() &&
         !machine.flash_active();
}

}  // namespace

esp_err_t maybe_run_boot_hub_trust_reset(const LaserController &laser) {
#if STAGECORE_LASER_LOCAL_RECOVERY_QUALIFIED == 0
  (void)laser;
  return ESP_OK;
#else
  constexpr int recovery_gpio = STAGECORE_LASER_LOCAL_RECOVERY_GPIO;

  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << static_cast<unsigned>(recovery_gpio);
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;

  esp_err_t err = gpio_config(&config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "local recovery GPIO init failed: %s",
             esp_err_to_name(err));
    return err;
  }

  const auto pin = static_cast<gpio_num_t>(recovery_gpio);
  if (gpio_get_level(pin) != 0) {
    return ESP_OK;
  }

  if (!known_safe_off(laser)) {
    ESP_LOGE(
        kTag,
        "physical recovery hold refused: StageLaser is not known DISARMED/OFF");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGW(kTag,
           "physical recovery hold detected; keep asserted for %u ms to clear "
           "remembered Hub trust only",
           static_cast<unsigned>(kTrustResetHoldMs));

  stagecore::LocalRecoveryHoldPolicy policy(kTrustResetHoldMs);
  while (gpio_get_level(pin) == 0) {
    if (policy.sample(true, kPollMs)) {
      err = stagecore::clear_hub_binding();
      if (err != ESP_OK) {
        ESP_LOGE(kTag, "Hub trust reset failed: %s", esp_err_to_name(err));
        return err;
      }

      ESP_LOGW(kTag,
               "remembered Hub trust cleared; Wi-Fi, device identity, laser "
               "truth, timing limits and replay fencing preserved");
      return ESP_OK;
    }

    vTaskDelay(pdMS_TO_TICKS(kPollMs));
  }

  // Released before the threshold: no persistent state was changed.
  (void)policy.sample(false, 0);
  ESP_LOGI(kTag, "physical recovery hold released before trust reset");
  return ESP_OK;
#endif
}

}  // namespace stagecore::stagelaser
