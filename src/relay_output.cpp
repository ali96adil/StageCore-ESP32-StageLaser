#include "relay_output.h"

#include <atomic>

#include "driver/gpio.h"
#include "esp_log.h"

#ifndef STAGECORE_LASER_ACTUATION_ENABLED
#define STAGECORE_LASER_ACTUATION_ENABLED 0
#endif
#ifndef STAGECORE_LASER_OUTPUT_GPIO
#define STAGECORE_LASER_OUTPUT_GPIO -1
#endif
#ifndef STAGECORE_LASER_SHARED_POWER_QUALIFIED
#define STAGECORE_LASER_SHARED_POWER_QUALIFIED 0
#endif
#ifndef STAGECORE_LASER_RELAY_DRIVER_QUALIFIED
#define STAGECORE_LASER_RELAY_DRIVER_QUALIFIED 0
#endif
#ifndef STAGECORE_LASER_INDEPENDENT_INTERLOCK_QUALIFIED
#define STAGECORE_LASER_INDEPENDENT_INTERLOCK_QUALIFIED 0
#endif
#ifndef STAGECORE_VISIBLE_LAMP_PROFILE
#define STAGECORE_VISIBLE_LAMP_PROFILE 0
#endif

// The generic CI/default firmware remains no-actuation.
// The explicit VISIBLE_LAMP profile is for an ordinary stage light whose
// momentary pushbutton is driven through a GPIO3 -> NPN -> 5V relay circuit.
// It must never be used on a genuine laser or other hazardous emitter.
// The output is active-HIGH at the ESP GPIO only when driving a qualified NPN
// pull-to-GND interface; NEVER connect the 5V active-low relay IN to the ESP.
#if STAGECORE_VISIBLE_LAMP_PROFILE != 0 && STAGECORE_VISIBLE_LAMP_PROFILE != 1
#error "Stage lamp profile must be exactly 0 or 1"
#endif
#if STAGECORE_VISIBLE_LAMP_PROFILE == 1 && STAGECORE_LASER_ACTUATION_ENABLED != 1
#error "Visible lamp profile must be an explicit enabled actuation build"
#endif

#if STAGECORE_LASER_ACTUATION_ENABLED == 1
#if STAGECORE_LASER_OUTPUT_GPIO != 3
#error "StageLaser actuation requires separately qualified GPIO3 hardware profile"
#endif
#if STAGECORE_LASER_RELAY_DRIVER_QUALIFIED != 1
#error "StageLaser relay driver/boot/reset qualification is required"
#endif
// StageLaser is a legacy product/protocol name. A visible stage lamp is NOT a
// laser-emission device. The non-laser lamp GPIO3 profile uses a momentary
// pushbutton relay and therefore does not require a laser beam interlock.
// Actual laser-emission hardware retains its independent interlock requirement.
#if STAGECORE_VISIBLE_LAMP_PROFILE != 1 && STAGECORE_LASER_INDEPENDENT_INTERLOCK_QUALIFIED != 1
#error "Real laser emission requires an independently qualified beam interlock"
#endif
#if STAGECORE_LASER_SHARED_POWER_QUALIFIED != 1
#error "StageLaser true cold-power OFF qualification is required"
#endif
#elif STAGECORE_LASER_ACTUATION_ENABLED != 0
#error "StageLaser actuation flag must be 0 or 1"
#endif

namespace stagecore::stagelaser {
namespace {
constexpr char kTag[] = "stagelaser-relay";
std::atomic<bool> output_ready{false};
std::atomic<bool> output_picked{false};

#if STAGECORE_LASER_ACTUATION_ENABLED == 1
constexpr gpio_num_t kOutputGPIO =
    static_cast<gpio_num_t>(STAGECORE_LASER_OUTPUT_GPIO);
#endif
}  // namespace

bool relay_actuation_enabled() {
#if STAGECORE_LASER_ACTUATION_ENABLED == 1
  return output_ready.load(std::memory_order_acquire);
#else
  return false;
#endif
}

esp_err_t relay_output_init() {
  output_ready.store(false, std::memory_order_release);
  output_picked.store(false, std::memory_order_release);
#if STAGECORE_LASER_ACTUATION_ENABLED == 1
  // Preload the GPIO LOW before changing the output direction. LOW or
  // high-impedance must leave the external default-OFF NPN stage released.
  esp_err_t err = gpio_set_level(kOutputGPIO, 0);
  if (err != ESP_OK) return err;
  gpio_config_t cfg{};
  cfg.pin_bit_mask = 1ULL << STAGECORE_LASER_OUTPUT_GPIO;
  cfg.mode = GPIO_MODE_OUTPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  err = gpio_config(&cfg);
  if (err != ESP_OK) return err;
  err = gpio_set_level(kOutputGPIO, 0);
  if (err != ESP_OK) return err;
  output_ready.store(true, std::memory_order_release);
  ESP_LOGI(kTag, "qualified driver initialized RELEASED on GPIO3");
#else
  ESP_LOGW(kTag, "NO-ACTUATION build: relay GPIO is intentionally disabled");
#endif
  return ESP_OK;
}

esp_err_t relay_pick() {
#if STAGECORE_LASER_ACTUATION_ENABLED == 1
  if (!output_ready.load(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
  bool expected = false;
  if (!output_picked.compare_exchange_strong(expected, true,
                                              std::memory_order_acq_rel)) {
    return ESP_ERR_INVALID_STATE;
  }
  // HIGH turns on the qualified NPN; its collector sinks the relay IN.
  const esp_err_t err = gpio_set_level(kOutputGPIO, 1);
  if (err != ESP_OK) {
    // Best-effort electrical release; do not treat failed PICK as success.
    (void)gpio_set_level(kOutputGPIO, 0);
    output_picked.store(false, std::memory_order_release);
  }
  return err;
#else
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t relay_release() {
#if STAGECORE_LASER_ACTUATION_ENABLED == 1
  if (!output_ready.load(std::memory_order_acquire)) return ESP_ERR_INVALID_STATE;
  // Even if the software missed a PICK flag, assert RELEASED explicitly.
  const esp_err_t err = gpio_set_level(kOutputGPIO, 0);
  if (err == ESP_OK) {
    output_picked.store(false, std::memory_order_release);
  }
  return err;
#else
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

}  // namespace stagecore::stagelaser
