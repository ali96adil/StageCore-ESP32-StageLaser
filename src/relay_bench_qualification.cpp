#include "relay_bench_qualification.h"

#ifndef STAGECORE_RELAY_BENCH_ONLY
#define STAGECORE_RELAY_BENCH_ONLY 0
#endif

#if STAGECORE_RELAY_BENCH_ONLY == 1

#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef STAGECORE_RELAY_BENCH_GPIO
#define STAGECORE_RELAY_BENCH_GPIO -1
#endif
#ifndef STAGECORE_LASER_ACTUATION_ENABLED
#define STAGECORE_LASER_ACTUATION_ENABLED 0
#endif
#ifndef STAGECORE_GPIO_NO_LOAD_QUALIFICATION
#define STAGECORE_GPIO_NO_LOAD_QUALIFICATION 0
#endif

#if STAGECORE_LASER_ACTUATION_ENABLED != 0 || STAGECORE_GPIO_NO_LOAD_QUALIFICATION != 0
#error "Relay bench cannot coexist with production actuation or GPIO no-load qualification"
#endif

static_assert(STAGECORE_RELAY_BENCH_GPIO == 3,
              "The manually approved bench candidate is GPIO3 only");

namespace stagecore::stagelaser {
namespace {
constexpr char kTag[] = "stagelaser-relay-bench";
constexpr gpio_num_t kPin = static_cast<gpio_num_t>(STAGECORE_RELAY_BENCH_GPIO);
constexpr uint32_t kPulseMs = 180;

[[noreturn]] void halt_safe(const char* reason) {
  gpio_set_level(kPin, 0);
  ESP_LOGE(kTag, "BENCH STOP: %s", reason);
  while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}
}  // namespace

[[noreturn]] void run_relay_bench_qualification() {
  // Set output latch LOW before changing direction; the reviewed NPN
  // pull-to-ground interface is expected to release its relay at LOW.
  if (gpio_set_level(kPin, 0) != ESP_OK) halt_safe("cannot set safe output latch");

  gpio_config_t cfg{};
  cfg.pin_bit_mask = (1ULL << STAGECORE_RELAY_BENCH_GPIO);
  cfg.mode = GPIO_MODE_OUTPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&cfg) != ESP_OK || gpio_set_level(kPin, 0) != ESP_OK)
    halt_safe("cannot configure safe relay output");

  ESP_LOGW(kTag, "RELAY-ONLY BENCH MODE: NO LASER CONNECTED TO COM/NO/NC");
  ESP_LOGW(kTag, "No Wi-Fi, StageCore connection, automatic pulse, or repeated pulse");
  ESP_LOGW(kTag, "Keep laser power OFF and laser leads physically disconnected");
  ESP_LOGI(kTag, "GPIO3 LOW; relay must be released");
  ESP_LOGI(kTag, "To issue ONE manual 180 ms pulse, type PULSE and press Enter");

  char line[40]{};
  bool fired = false;
  while (true) {
    if (std::fgets(line, sizeof(line), stdin) == nullptr) {
      clearerr(stdin);
      vTaskDelay(pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1);
      continue;
    }
    size_t len = std::strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
      line[--len] = '\0';
    }
    if (std::strcmp(line, "PULSE") != 0) {
      ESP_LOGW(kTag, "Ignored input; exact command is PULSE");
      continue;
    }
    if (fired) {
      ESP_LOGW(kTag, "One-pulse limit reached; reset with laser disconnected to retest");
      continue;
    }
    fired = true;  // latch before driving the output
    ESP_LOGW(kTag, "Manual bench pulse starting: 180 ms, GPIO3 HIGH then LOW");
    if (gpio_set_level(kPin, 1) != ESP_OK) halt_safe("cannot assert output");
    vTaskDelay(pdMS_TO_TICKS(kPulseMs) > 0 ? pdMS_TO_TICKS(kPulseMs) : 1);
    if (gpio_set_level(kPin, 0) != ESP_OK) halt_safe("cannot release output");
    ESP_LOGI(kTag, "BENCH PULSE COMPLETE; GPIO3 LOW, further pulses locked");
  }
}

}  // namespace stagecore::stagelaser
#endif
