from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PersistentLimitsContract(unittest.TestCase):
    def test_store_persists_all_output_limits(self):
        source = (ROOT / "src" / "laser_limits_store.cpp").read_text()
        for key in [
            '"pulse_ms"',
            '"rest_ms"',
            '"min_mhz"',
            '"max_mhz"',
            '"duration_ms"',
        ]:
            self.assertIn(key, source)
        self.assertIn("nvs_commit", source)
        self.assertIn("limits_valid(loaded)", source)

    def test_main_loads_limits_and_passes_them_to_controller(self):
        main = (ROOT / "src" / "main.cpp").read_text()
        self.assertIn("load_persistent_limits", main)
        self.assertIn("save_persistent_limits", main)
        self.assertIn("laser timing limits unavailable or invalid", main)
        self.assertIn("&laser_store, &laser_actuator, limits", main)

    def test_persistent_limits_are_separate_from_physical_truth_state(self):
        state = (ROOT / "src" / "laser_state_store.cpp").read_text()
        limits = (ROOT / "src" / "laser_limits_store.cpp").read_text()
        self.assertIn('"laser_state"', state)
        self.assertIn('"laser_limits"', limits)
        self.assertNotIn('"project_id"', limits)
        self.assertNotIn('"runtime_snapshot_id"', limits)


if __name__ == "__main__":
    unittest.main()
