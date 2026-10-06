from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ControllerMainWiringContract(unittest.TestCase):
    def test_main_uses_transaction_controller_as_truth_owner(self):
        source = (ROOT / "src" / "main.cpp").read_text()
        self.assertIn("LaserController laser(", source)
        self.assertIn("NvsPersistentStateSink", source)
        self.assertIn("RelayOutputActuator", source)
        self.assertNotIn("StateMachine laser;", source)

if __name__ == "__main__":
    unittest.main()
