import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class OtaRollbackContractTest(unittest.TestCase):
    def test_candidate_enables_application_rollback(self):
        overlay = (ROOT / "sdkconfig.ota_candidate.defaults").read_text()
        self.assertIn("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y", overlay)
        self.assertIn("# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set", overlay)

        platformio = (ROOT / "platformio.ini").read_text()
        match = re.search(
            r"\[env:esp32c3-ota-candidate-no-actuation\](.*?)(?:\n\[|\Z)",
            platformio,
            re.S,
        )
        self.assertIsNotNone(match)
        section = match.group(1)
        normalized = section.replace("\\\"", '"')
        self.assertIn(
            'SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ota_candidate.defaults"',
            normalized,
        )

    def test_safe_boot_guard_is_wired_before_provisioning(self):
        main = (ROOT / "src/main.cpp").read_text()
        load_config = main.index("load_device_config(&config)")
        confirm = main.index("ota_confirm_safe_boot_if_pending()")
        provisioning = main.index("run_provisioning_portal(")

        self.assertLess(load_config, confirm)
        self.assertLess(confirm, provisioning)
        self.assertIn(
            'hold_safe_failure("OTA candidate safe-boot confirmation failed")',
            main,
        )

    def test_guard_only_confirms_pending_image(self):
        guard = (ROOT / "src/ota_boot_guard.cpp").read_text()
        self.assertIn("ESP_OTA_IMG_PENDING_VERIFY", guard)
        self.assertIn("esp_ota_mark_app_valid_cancel_rollback()", guard)
        self.assertNotIn("esp_ota_mark_app_invalid_rollback", guard)
        self.assertNotIn("esp_ota_begin(", guard)
        self.assertNotIn("esp_ota_write(", guard)

    def test_app_update_component_is_explicit(self):
        cmake = (ROOT / "src/CMakeLists.txt").read_text()
        self.assertIn('"ota_boot_guard.cpp"', cmake)
        self.assertRegex(cmake, r"(?m)^\s+app_update\s*$")


if __name__ == "__main__":
    unittest.main()
