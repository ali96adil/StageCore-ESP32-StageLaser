from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GPIO3NoLoadQualificationContract(unittest.TestCase):
    def test_default_environment_does_not_enable_gpio_qualification(self):
        ini = (ROOT / "platformio.ini").read_text()
        default_block = ini.split(
            "[env:esp32c3-ci-no-actuation]", 1
        )[1].split("[env:", 1)[0]
        self.assertNotIn("STAGECORE_GPIO_NO_LOAD_QUALIFICATION=1", default_block)
        self.assertIn("STAGECORE_LASER_ACTUATION_ENABLED=0", default_block)
        self.assertIn("STAGECORE_LASER_OUTPUT_GPIO=-1", default_block)

    def test_gpio3_environment_is_explicit_and_still_no_actuation(self):
        ini = (ROOT / "platformio.ini").read_text()
        block = ini.split(
            "[env:esp32c3-gpio3-no-load-qualification]", 1
        )[1]
        self.assertIn("extends = env:esp32c3-ci-no-actuation", block)
        self.assertIn("STAGECORE_GPIO_NO_LOAD_QUALIFICATION=1", block)
        self.assertIn("STAGECORE_GPIO_NO_LOAD_QUALIFICATION_GPIO=3", block)

    def test_qualification_code_never_drives_candidate_high(self):
        source = (ROOT / "src" / "gpio_no_load_qualification.cpp").read_text()
        self.assertIn("gpio_set_level(pin, 0)", source)
        self.assertNotIn("gpio_set_level(pin, 1)", source)
        self.assertIn("GPIO_MODE_OUTPUT", source)

    def test_qualification_init_runs_before_relay_runtime_init(self):
        main = (ROOT / "src" / "main.cpp").read_text()
        q = main.index("gpio_no_load_qualification_init")
        r = main.index("relay_output_init")
        self.assertLess(q, r)


if __name__ == "__main__":
    unittest.main()
