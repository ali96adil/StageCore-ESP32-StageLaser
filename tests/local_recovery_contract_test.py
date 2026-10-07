import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class LocalRecoveryContractTest(unittest.TestCase):
    def test_default_and_ota_images_leave_recovery_unqualified(self):
        platformio = (ROOT / "platformio.ini").read_text()

        for env_name in (
            "esp32c3-ci-no-actuation",
            "esp32c3-ota-candidate-no-actuation",
        ):
            match = re.search(
                rf"\[env:{re.escape(env_name)}\](.*?)(?:\n\[|\Z)",
                platformio,
                re.S,
            )
            self.assertIsNotNone(match)
            section = match.group(1)
            self.assertIn(
                "-DSTAGECORE_LASER_LOCAL_RECOVERY_GPIO=-1",
                section,
            )
            self.assertIn(
                "-DSTAGECORE_LASER_LOCAL_RECOVERY_QUALIFIED=0",
                section,
            )

    def test_recovery_is_non_actuating_and_trust_only(self):
        source = (ROOT / "src" / "local_recovery.cpp").read_text()

        self.assertIn("clear_hub_binding()", source)
        self.assertIn("known_safe_off", source)
        self.assertIn("kTrustResetHoldMs = 10000", source)
        self.assertIn("LOCAL_RECOVERY_QUALIFIED == 1", source)

        forbidden = (
            "SafeOff(",
            "SetOff(",
            "SetOn(",
            "FlashStart(",
            "FlashStop(",
            "Pick(",
            "Release(",
            "relay_output",
            "esp_restart(",
            "httpd_",
        )
        for token in forbidden:
            self.assertNotIn(token, source)

    def test_recovery_runs_before_ota_confirmation_and_network(self):
        main = (ROOT / "src" / "main.cpp").read_text()

        config = main.index("load_device_config(&config)")
        recovery = main.index("maybe_run_boot_hub_trust_reset(laser)")
        ota = main.index("ota_confirm_safe_boot_if_pending()")
        provisioning = main.index("run_provisioning_portal(")
        network = main.index("connect_station(")

        self.assertLess(config, recovery)
        self.assertLess(recovery, ota)
        self.assertLess(ota, provisioning)
        self.assertLess(provisioning, network)

    def test_build_links_recovery_source(self):
        cmake = (ROOT / "src" / "CMakeLists.txt").read_text()
        self.assertIn('"local_recovery.cpp"', cmake)


if __name__ == "__main__":
    unittest.main()
