from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]

class VisibleLampProfileContract(unittest.TestCase):
    def test_default_remains_no_actuation(self):
        ini = (ROOT / "platformio.ini").read_text()
        self.assertIn("default_envs = esp32c3-ci-no-actuation", ini)
        default = ini.split("[env:esp32c3-ci-no-actuation]", 1)[1].split("[env:", 1)[0]
        self.assertIn("-DSTAGECORE_LASER_ACTUATION_ENABLED=0", default)
        self.assertIn("-DSTAGECORE_LASER_OUTPUT_GPIO=-1", default)

    def test_lamp_is_an_explicit_gpio3_build_with_no_false_laser_interlock(self):
        ini = (ROOT / "platformio.ini").read_text()
        profile = ini.split("[env:esp32c3-stage-lamp-gpio3-live]", 1)[1]
        for required in (
            "-DSTAGECORE_LASER_ACTUATION_ENABLED=1",
            "-DSTAGECORE_LASER_OUTPUT_GPIO=3",
            "-DSTAGECORE_LASER_RELAY_DRIVER_QUALIFIED=1",
            "-DSTAGECORE_VISIBLE_LAMP_PROFILE=1",
            "-DSTAGECORE_LASER_SHARED_POWER_QUALIFIED=1",
            "-DSTAGECORE_OTA_ENABLED=0",
        ):
            self.assertIn(required, profile)
        self.assertNotIn("-DSTAGECORE_LASER_INDEPENDENT_INTERLOCK_QUALIFIED=1", profile)

    def test_gpio_driver_only_bypasses_laser_interlock_for_lamp_profile(self):
        source = (ROOT / "src" / "relay_output.cpp").read_text()
        self.assertIn("STAGECORE_VISIBLE_LAMP_PROFILE != 1 && STAGECORE_LASER_INDEPENDENT_INTERLOCK_QUALIFIED != 1", source)
        self.assertIn("STAGECORE_LASER_RELAY_DRIVER_QUALIFIED != 1", source)
        self.assertIn("STAGECORE_LASER_SHARED_POWER_QUALIFIED != 1", source)
        self.assertIn("STAGECORE_LASER_OUTPUT_GPIO != 3", source)
        self.assertIn("gpio_set_level(kOutputGPIO, 0)", source)
        self.assertIn("gpio_set_level(kOutputGPIO, 1)", source)
        self.assertIn("GPIO_MODE_OUTPUT", source)

    def test_show_notes_explicitly_warn_about_toggle_uncertainty(self):
        notes = (ROOT / "docs" / "STAGE_LAMP_GPIO3_LIVE.md").read_text()
        self.assertIn("not a laser emitter", notes.lower())
        self.assertIn("independently wired main power isolator", notes)
        self.assertIn("does not by itself switch the lamp", notes)

if __name__ == "__main__":
    unittest.main()
