from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FOUNDATION_SHA = "4495a381cc40f06f44e0a16da4a4dc82a9bceb6c"

class IdentityConfigContract(unittest.TestCase):
    def test_v2_config_has_no_project_authority(self):
        header = (ROOT / "src" / "config_store.h").read_text()
        source = (ROOT / "src" / "config_store.cpp").read_text()
        self.assertNotIn("project_id", header)
        self.assertNotIn("kProjectKey", source)

    def test_persistent_p256_identity_is_owned_by_pinned_foundation(self):
        main = (ROOT / "src" / "main.cpp").read_text()
        cmake = (ROOT / "src" / "CMakeLists.txt").read_text()
        manifest = (ROOT / "src" / "idf_component.yml").read_text()

        self.assertIn("stagecore::DeviceIdentity identity", main)
        self.assertIn("identity.LoadOrCreate()", main)
        self.assertIn("stagecore_foundation", cmake)
        self.assertIn(f"version: {FOUNDATION_SHA}", manifest)
        self.assertFalse((ROOT / "src" / "device_identity.cpp").exists())
        self.assertFalse((ROOT / "src" / "device_identity.h").exists())

if __name__ == "__main__":
    unittest.main()
