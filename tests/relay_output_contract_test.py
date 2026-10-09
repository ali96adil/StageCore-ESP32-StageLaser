"""Host-only GPIO mock tests. They never access physical hardware or a laser."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

GPIO_H = r"""
#pragma once
#include "esp_err.h"
using gpio_num_t = int;
enum { GPIO_MODE_OUTPUT = 1, GPIO_PULLUP_DISABLE = 0,
       GPIO_PULLDOWN_DISABLE = 0, GPIO_PULLDOWN_ENABLE = 1,
       GPIO_INTR_DISABLE = 0 };
struct gpio_config_t {
    unsigned long long pin_bit_mask{};
    int mode{};
    int pull_up_en{};
    int pull_down_en{};
    int intr_type{};
};
esp_err_t gpio_config(const gpio_config_t *);
esp_err_t gpio_set_level(gpio_num_t, int);
"""
ESP_ERR_H = """
#pragma once
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_ERR_NOT_SUPPORTED = 1;
constexpr int ESP_ERR_INVALID_STATE = 2;
"""
ESP_LOG_H = """
#pragma once
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
"""
MOCK_MAIN = r"""
#include <vector>
#include "relay_output.h"
#include "driver/gpio.h"
using namespace stagecore::stagelaser;
static std::vector<int> changes;
static int configurations = 0;
esp_err_t gpio_config(const gpio_config_t *cfg) {
    ++configurations;
    return cfg->pin_bit_mask == (1ULL << 3) ? ESP_OK : -10;
}
esp_err_t gpio_set_level(gpio_num_t gpio, int level) {
    if (gpio != 3) return -11;
    changes.push_back(level);
    return ESP_OK;
}
int main() {
#if STAGECORE_LASER_ACTUATION_ENABLED == 1
    if (relay_actuation_enabled()) return 1;
    if (relay_pick() != ESP_ERR_INVALID_STATE) return 2;
    if (relay_output_init() != ESP_OK) return 3;
    if (!relay_actuation_enabled()) return 4;
    if (configurations != 1 || changes != std::vector<int>({0, 0})) return 5;
    if (relay_pick() != ESP_OK) return 6;
    if (relay_pick() != ESP_ERR_INVALID_STATE) return 7;
    if (relay_release() != ESP_OK) return 8;
    if (relay_release() != ESP_OK) return 9;
    if (changes != std::vector<int>({0, 0, 1, 0, 0})) return 10;
#else
    if (relay_output_init() != ESP_OK) return 11;
    if (relay_actuation_enabled() || configurations || !changes.empty()) return 12;
    if (relay_pick() != ESP_ERR_NOT_SUPPORTED) return 13;
    if (relay_release() != ESP_ERR_NOT_SUPPORTED) return 14;
#endif
    return 0;
}
"""


class RelayOutputContractTest(unittest.TestCase):
    def compile(self, flags, expect_success=True):
        with tempfile.TemporaryDirectory() as path:
            temp = Path(path)
            (temp / "driver").mkdir()
            (temp / "driver" / "gpio.h").write_text(GPIO_H)
            (temp / "esp_err.h").write_text(ESP_ERR_H)
            (temp / "esp_log.h").write_text(ESP_LOG_H)
            (temp / "main.cpp").write_text(MOCK_MAIN)
            binary = temp / "test-relay"
            cmd = [
                "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-pthread", "-I" + str(temp), "-I" + str(ROOT / "src"),
                *["-D" + flag for flag in flags],
                str(ROOT / "src" / "relay_output.cpp"),
                str(temp / "main.cpp"), "-o", str(binary)
            ]
            result = subprocess.run(cmd, text=True, capture_output=True, check=False)
            if not expect_success:
                self.assertNotEqual(result.returncode, 0, "unqualified actuation must not build")
                self.assertIn("qualification is required", result.stderr)
                return
            self.assertEqual(result.returncode, 0, result.stderr)
            executed = subprocess.run([str(binary)], text=True, capture_output=True, check=False)
            self.assertEqual(executed.returncode, 0, executed.stderr)

    def test_default_build_has_zero_gpio_writes(self):
        self.compile([])

    def test_missing_interlock_rejects_actuation_at_compile_time(self):
        self.compile([
            "STAGECORE_LASER_ACTUATION_ENABLED=1",
            "STAGECORE_LASER_OUTPUT_GPIO=3",
            "STAGECORE_LASER_RELAY_DRIVER_QUALIFIED=1",
            "STAGECORE_LASER_SHARED_POWER_QUALIFIED=1",
        ], expect_success=False)

    def test_wrong_gpio_rejects_actuation_at_compile_time(self):
        self.compile([
            "STAGECORE_LASER_ACTUATION_ENABLED=1",
            "STAGECORE_LASER_OUTPUT_GPIO=4",
            "STAGECORE_LASER_RELAY_DRIVER_QUALIFIED=1",
            "STAGECORE_LASER_SHARED_POWER_QUALIFIED=1",
            "STAGECORE_LASER_INDEPENDENT_INTERLOCK_QUALIFIED=1",
        ], expect_success=False)

    def test_qualified_gpio3_sequence_is_idle_pick_release(self):
        self.compile([
            "STAGECORE_LASER_ACTUATION_ENABLED=1",
            "STAGECORE_LASER_OUTPUT_GPIO=3",
            "STAGECORE_LASER_RELAY_DRIVER_QUALIFIED=1",
            "STAGECORE_LASER_SHARED_POWER_QUALIFIED=1",
            "STAGECORE_LASER_INDEPENDENT_INTERLOCK_QUALIFIED=1",
        ])


if __name__ == "__main__":
    unittest.main()
