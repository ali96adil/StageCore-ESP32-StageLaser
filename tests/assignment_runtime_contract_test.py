from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ActiveRuntimeContract(unittest.TestCase):
    def test_v2_hello_is_projectless(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        self.assertIn('"type", "device.hello"', source)
        self.assertIn('"protocol_version", kProtocolVersion', source)
        self.assertNotIn('"project_id", config.', source)

    def test_assignment_and_active_scope_handshake_are_present(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        for token in [
            '"assignment.state"',
            '"stagelaser.assignment.prepare"',
            '"stagelaser.assignment.safe_ack"',
            '"stagelaser.assignment.scope_ack"',
            '"runtime.ready"',
        ]:
            self.assertIn(token, source)
        self.assertIn("drive_safe_off", source)
        self.assertIn("known_safe_off", source)

    def test_commands_are_gated_by_hub_ready_scope(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        self.assertIn('"command.execute"', source)
        self.assertIn("context->commands_enabled", source)
        self.assertIn("kRuntimeReadyBit", source)
        self.assertIn("evaluate_command_execute_frame", source)
        self.assertIn("context.project_id", source)
        self.assertIn("context.runtime_snapshot_id", source)

    def test_runtime_exit_attempts_deterministic_safe_off(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        self.assertIn("if (context.commands_enabled)", source)
        self.assertIn("runtime ended and deterministic Safe Off", source)
        self.assertNotIn("LASER_TOGGLE", source)

    def test_safe_ack_and_scope_ack_require_known_disarmed_off(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        self.assertIn("ArmState::kDisarmed", source)
        self.assertIn("LogicalState::kOff", source)
        self.assertIn("StateQuality::kTracked", source)
        self.assertIn("StateQuality::kConfirmed", source)
        self.assertIn("resync_required()", source)

if __name__ == "__main__":
    unittest.main()
