from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class IdentitySurfaceContract(unittest.TestCase):
    def test_pairing_hostname_uses_stagelaser_prefix(self):
        source = (ROOT / "src" / "hub_security.cpp").read_text()
        self.assertIn('return "stagecore-laser-" + compact;', source)
        self.assertNotIn('stagecore-light-', source)

    def test_fallback_firmware_version_matches_platformio_identity(self):
        expected = '0.1.0-dev.1'
        hub = (ROOT / "src" / "hub_security.cpp").read_text()
        main = (ROOT / "src" / "main.cpp").read_text()
        self.assertIn(f'#define STAGECORE_FW_VERSION "{expected}"', hub)
        self.assertIn(f'#define STAGECORE_FW_VERSION "{expected}"', main)


if __name__ == "__main__":
    unittest.main()
