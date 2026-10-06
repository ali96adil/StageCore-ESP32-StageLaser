from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CIBoardGeometryContract(unittest.TestCase):
    def test_ci_geometry_is_explicitly_not_hardware_qualification(self):
        ini = (ROOT / "platformio.ini").read_text()
        self.assertIn("CI-only generic board geometry", ini)
        self.assertIn("NOT a StageLaser hardware qualification", ini)
        self.assertIn("partitions-ci-4mb.csv", ini)

    def test_ci_partition_has_room_for_trust_stack(self):
        csv = (ROOT / "partitions-ci-4mb.csv").read_text()
        self.assertIn("0x200000", csv)
        self.assertIn("factory", csv)

if __name__ == "__main__":
    unittest.main()
