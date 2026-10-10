from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ESPControllerAdapterContract(unittest.TestCase):
    def test_adapter_uses_only_guarded_relay_output(self):
        source = (ROOT / "src" / "laser_controller_esp.cpp").read_text()
        self.assertIn("relay_pick()", source)
        self.assertIn("relay_release()", source)
        self.assertNotIn("gpio_set_level", source)

    def test_fatal_startup_parks_relay_output_without_claiming_beam_off(self):
        main = (ROOT / "src" / "main.cpp").read_text()
        fatal = main.split("[[noreturn]] void hold_safe_failure(", 1)[1].split(
            "std::string default_display_name(", 1
        )[0]
        self.assertIn("relay_actuation_enabled()", fatal)
        self.assertIn("relay_release()", fatal)
        self.assertIn("physical beam state unverified", fatal)
        self.assertLess(fatal.index("relay_release()"), fatal.index("while (true)"))

    def test_adapter_persists_through_truth_store(self):
        source = (ROOT / "src" / "laser_controller_esp.cpp").read_text()
        self.assertIn("save_persistent_state", source)

if __name__ == "__main__":
    unittest.main()
