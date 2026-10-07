from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NVSStartupSafetyContract(unittest.TestCase):
    def test_boot_never_auto_erases_nvs(self):
        main = (ROOT / "src" / "main.cpp").read_text()
        self.assertIn("return nvs_flash_init();", main)
        self.assertNotIn("nvs_flash_erase()", main)

    def test_nvs_failure_enters_safe_hold(self):
        main = (ROOT / "src" / "main.cpp").read_text()
        self.assertIn("automatic erase is prohibited", main)
        self.assertIn('hold_safe_failure("persistent safety state unavailable")', main)

    def test_failure_happens_before_runtime_or_pairing(self):
        main = (ROOT / "src" / "main.cpp").read_text()
        failure = main.index("persistent safety state unavailable")
        identity = main.index("DeviceIdentity identity")
        runtime = main.index("run_stage_device_runtime")
        self.assertLess(failure, identity)
        self.assertLess(failure, runtime)


if __name__ == "__main__":
    unittest.main()
