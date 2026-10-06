from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class AssignmentRuntimeContract(unittest.TestCase):
    def test_v2_hello_is_projectless(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        self.assertIn('"type", "device.hello"', source)
        self.assertIn('"protocol_version", kProtocolVersion', source)
        self.assertNotIn('"project_id", config.', source)

    def test_assignment_slice_accepts_only_safe_handshake(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        self.assertIn('"assignment.state"', source)
        self.assertIn('"stagelaser.assignment.prepare"', source)
        self.assertIn('"stagelaser.assignment.safe_ack"', source)
        self.assertIn('drive_safe_off', source)
        self.assertNotIn('"command.execute"', source)
        self.assertNotIn('"stagelaser.assignment.scope_ack"', source)
        self.assertNotIn('"runtime.ready"', source)

    def test_safe_ack_requires_known_disarmed_off(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        self.assertIn("ArmState::kDisarmed", source)
        self.assertIn("LogicalState::kOff", source)
        self.assertIn("StateQuality::kTracked", source)
        self.assertIn("StateQuality::kConfirmed", source)
        self.assertIn("resync_required()", source)

if __name__ == "__main__":
    unittest.main()
