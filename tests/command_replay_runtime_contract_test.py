from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class PersistentCommandReplayRuntimeContract(unittest.TestCase):
    def test_actuating_commands_cross_replay_fence_before_controller(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        remember = source.index("command_replay_remember(command.command_id)")
        controller = source.index("stagelaser::ControllerOutcome outcome")
        self.assertLess(remember, controller)
        for command in [
            "LASER_DISARM", "LASER_SET_ON", "LASER_SET_OFF",
            "LASER_FLASH_START", "LASER_FLASH_STOP", "LASER_SAFE_OFF",
        ]:
            self.assertIn(command, source)

    def test_replay_is_terminal_without_actuation(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        self.assertIn('"DEVICE_COMMAND_DUPLICATE"', source)
        self.assertIn('"IDEMPOTENCY"', source)
        self.assertIn("command_replay_seen", source)

if __name__ == "__main__":
    unittest.main()
