from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class TypedCommandPayloadContract(unittest.TestCase):
    def test_validated_payload_is_captured_as_typed_fields(self):
        header = (ROOT / "src" / "laser_command_contract.h").read_text()
        source = (ROOT / "src" / "laser_command_contract.cpp").read_text()
        self.assertIn("flash_frequency_hz", header)
        self.assertIn("flash_duration_ms", header)
        self.assertIn("resync_state", header)
        self.assertIn('parsed.command_type == "LASER_FLASH_START"', source)
        self.assertIn('parsed.command_type == "LASER_STATE_RESYNC"', source)

if __name__ == "__main__":
    unittest.main()
