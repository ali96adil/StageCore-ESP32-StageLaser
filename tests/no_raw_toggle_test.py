from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class NoRawToggle(unittest.TestCase):
    def test_no_public_toggle_contract(self):
        source_files = list((ROOT / "src").rglob("*")) + [ROOT / "README.md", ROOT / "docs" / "FIRMWARE_PLAN.md"]
        text = "\n".join(p.read_text(errors="ignore") for p in source_files if p.is_file())
        self.assertNotIn("LASER_TOGGLE", text)
        self.assertNotIn("laser.toggle", text)

    def test_default_build_is_no_actuation(self):
        ini = (ROOT / "platformio.ini").read_text()
        self.assertIn("-DSTAGECORE_LASER_ACTUATION_ENABLED=0", ini)
        self.assertIn("-DSTAGECORE_LASER_OUTPUT_GPIO=-1", ini)

if __name__ == "__main__":
    unittest.main()
