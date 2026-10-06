from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class IdentitySurfaceContract(unittest.TestCase):
    def test_pairing_hostname_uses_stagelaser_prefix(self):
        source = (ROOT / "src" / "hub_security.cpp").read_text()
        self.assertIn('return "stagecore-laser-" + compact;', source)
        self.assertNotIn('stagecore-light-', source)


if __name__ == "__main__":
    unittest.main()
