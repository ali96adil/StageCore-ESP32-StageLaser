#include "gpio_no_load_qualification.h"

#include "driver/gpio.h"
#include "esp_log.h"

#ifndef STAGECORE_GPIO_NO_LOAD_QUALIFICATION
#define STAGECORE_GPIO_NO_LOAD_QUALIFICATION 0
#endif

#ifndef STAGECORE_GPIO_NO_LOAD_QUALIFICATION_GPIO
#define STAGECORE_GPIO_NO_LOAD_QUALIFICATION_GPIO -1
#endif

namespace stagecore::stagelaser {
namespace {
constexpr char kTag[] = "stagelaser-gpio-qual";
}

bool gpio_no_load_qualification_enabled() {
  return STAGECORE_GPIO_NO_LOAD_QUALIFICATION == 1 &&
         STAGECORE_GPIO_NO_LOAD_QUALIFICATION_GPIO >= 0;
}

esp_err_t gpio_no_load_qualification_init() {
  if (!gpio_no_load_qualification_enabled()) return ESP_OK;

  const auto pin =
      static_cast<gpio_num_t>(STAGECORE_GPIO_NO_LOAD_QUALIFICATION_GPIO);

  // Preload the output latch LOW before enabling output direction. For the
  // proposed NPN interface, LOW means transistor OFF / relay released.
  esp_err_t err = gpio_set_level(pin, 0);
  if (err != ESP_OK) return err;

  gpio_config_t cfg{};
  cfg.pin_bit_mask = 1ULL << STAGECORE_GPIO_NO_LOAD_QUALIFICATION_GPIO;
  cfg.mode = GPIO_MODE_OUTPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  err = gpio_config(&cfg);
  if (err != ESP_OK) return err;

  err = gpio_set_level(pin, 0);
  if (err != ESP_OK) return err;

  ESP_LOGW(kTag,
           "GPIO%d NO-LOAD QUALIFICATION: candidate held LOW; relay and laser "
           "must remain disconnected",
           STAGECORE_GPIO_NO_LOAD_QUALIFICATION_GPIO);
  return ESP_OK;
}

}  // namespace stagecore::stagelaser
