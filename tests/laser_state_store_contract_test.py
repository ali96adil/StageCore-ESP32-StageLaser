from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class LaserStateStoreContract(unittest.TestCase):
    def test_store_persists_truth_markers(self):
        source = (ROOT / "src" / "laser_state_store.cpp").read_text()
        for key in ["kTransitionKey", "kFlashKey", "kPulseCountKey", "nvs_commit"]:
            self.assertIn(key, source)

    def test_store_has_no_hub_scope_authority(self):
        text = "\n".join([
            (ROOT / "src" / "laser_state_store.h").read_text(),
            (ROOT / "src" / "laser_state_store.cpp").read_text(),
        ])
        for forbidden in [
            "project_id", "runtime_snapshot", "assignment_epoch",
            "commands_enabled", "session_token",
        ]:
            self.assertNotIn(forbidden, text)

    def test_ci_does_not_claim_shared_power_qualification(self):
        ini = (ROOT / "platformio.ini").read_text()
        self.assertIn("-DSTAGECORE_LASER_SHARED_POWER_QUALIFIED=0", ini)

if __name__ == "__main__":
    unittest.main()
