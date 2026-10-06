#include "relay_output.h"

#include "driver/gpio.h"
#include "esp_log.h"

#ifndef STAGECORE_LASER_ACTUATION_ENABLED
#define STAGECORE_LASER_ACTUATION_ENABLED 0
#endif
#ifndef STAGECORE_LASER_OUTPUT_GPIO
#define STAGECORE_LASER_OUTPUT_GPIO+ -1
#endif

namespace stagecore::stagelaser {
namespace {
constexpr char kTag[] = "stagelaser-relay";
}

bool relay_actuation_enabled() {
  return STAGECORE_LASER_ACTUATION_ENABLED == 1 && STAGECORE_LASER_OUTPUT_GPIO >= 0;
}

esp_err_t relay_output_init() {
  if (!relay_actuation_enabled()) {
    ESP_LOGW(kTag, "NO-ACTUATION build: relay GPIO is intentionally disabled");
    return ESP_OK;
  }
#if STAGECORE_LASER_ACTUATION_ENABLED == 1
  gpio_config_t cfg{};
  cfg.pin_bit_mask = 1ULL << STAGECORE_LASER_OUTPUT_GPIO;
  cfg.mode = GPIO_MODE_OUTPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLD;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLD;
  esp_err_t err = gpio_config(&cfg);
  if (err != ESP_OK) return err;
  // Polarity is deliberately not implemented until the physical relay module
  // is qualified. This prevents an unreviewed reset default from closing the contact.
  ESP_LOGE(kTag, "actuation build requested but relay polarity is not qualified");
  return ESP_ERR_NOT_SUPPORTED;
#else
  return ESP_OK;
#endif

}

esp_err_t relay_pick() {
  if (!relay_actuation_enabled()) return ESP_ERR_NOT_SUPPORTED;
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t relay_release() {
  if (!relay_actuation_enabled()) return ESP_ERR_NOT_SUPPORTED;
  return ESP_ERR_NOT_SUPPORTED;
}

}  // namespace stagecore::stagelaser
