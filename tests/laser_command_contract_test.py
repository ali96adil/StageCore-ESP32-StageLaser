from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class LaserCommandContract(unittest.TestCase):
    def test_only_official_stagelaser_commands_are_supported(self):
        source = (ROOT / "src" / "laser_command_contract.cpp").read_text()
        expected = [
            "LASER_ARM", "LASER_DISARM", "LASER_SET_ON", "LASER_SET_OFF",
            "LASER_FLASH_START", "LASER_FLASH_STOP", "LASER_SAFE_OFF",
            "LASER_STATE_READ", "LASER_STATE_RESYNC",
        ]
        for command in expected:
            self.assertIn(f'"{command}"', source)
        self.assertNotIn("LASER_TOGGLE", source)
        self.assertNotIn("LIGHTING_", source)

    def test_flash_and_resync_payloads_are_bounded(self):
        source = (ROOT / "src" / "laser_command_contract.cpp").read_text()
        self.assertIn('"frequency_hz"', source)
        self.assertIn('"duration_ms"', source)
        self.assertIn("limits.max_flash_hz", source)
        self.assertIn("limits.max_flash_duration_ms", source)
        self.assertIn('"state"', source)
        self.assertIn('"OFF"', source)
        self.assertIn('"ON"', source)

    def test_journal_is_bounded_and_can_hold_accepted_response(self):
        header = (ROOT / "src" / "laser_command_contract.h").read_text()
        source = (ROOT / "src" / "laser_command_contract.cpp").read_text()
        self.assertIn("kCapacity = 32", header)
        self.assertIn("CommandJournal::Remember", source)
        self.assertNotIn("terminal_result_json", header)

    def test_exact_project_snapshot_and_deadline_are_checked(self):
        source = (ROOT / "src" / "laser_command_contract.cpp").read_text()
        self.assertIn("parsed.project_id != expected_project_id", source)
        self.assertIn("parsed.runtime_snapshot_id != expected_runtime_snapshot_id", source)
        self.assertIn("trusted_clock_ready()", source)
        self.assertIn("DEVICE_COMMAND_EXPIRED", source)

if __name__ == "__main__":
    unittest.main()
