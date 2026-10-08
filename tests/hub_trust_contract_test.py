from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FOUNDATION_SHA = "d6946da3f003e8c0c2a72216804ef2035bf1288c"

class HubTrustContract(unittest.TestCase):
    def test_discovery_and_trust_are_owned_by_pinned_foundation(self):
        main = (ROOT / "src" / "main.cpp").read_text()
        config = (ROOT / "src" / "config_store.cpp").read_text()
        manifest = (ROOT / "src" / "idf_component.yml").read_text()

        self.assertIn("stagecore_foundation:", manifest)
        self.assertIn(f"version: {FOUNDATION_SHA}", manifest)
        self.assertIn("discover_and_verify_hub(&foundation, &hub)", main)
        self.assertIn("foundation_store().LoadHubBinding", config)
        self.assertIn("foundation_store().SaveHubBinding", config)
        self.assertIn("foundation_store().ClearHubBinding", config)
        self.assertFalse((ROOT / "src" / "hub_discovery.cpp").exists())
        self.assertFalse((ROOT / "src" / "hub_security.cpp").exists())

    def test_pairing_descriptor_preserves_stagelaser_surface(self):
        source = (ROOT / "src" / "main.cpp").read_text()
        for capability in [
            "laser.arm", "laser.disarm", "laser.state.set",
            "laser.flash.start", "laser.flash.stop", "laser.safe_off",
            "laser.state.read", "laser.state.resync",
        ]:
            self.assertIn(capability, source)
        self.assertNotIn("lighting.", source)
        self.assertIn('descriptor.architecture = "riscv32"', source)
        self.assertIn('descriptor.hostname_prefix = "stagecore-laser-"', source)
        self.assertIn("descriptor.firmware_version = STAGECORE_FW_VERSION", source)

    def test_partial_hub_trust_uses_fail_closed_foundation_store(self):
        header = (ROOT / "src" / "config_store.h").read_text()
        source = (ROOT / "src" / "config_store.cpp").read_text()
        manifest = (ROOT / "src" / "idf_component.yml").read_text()

        self.assertIn('#include "foundation_store.h"', header)
        self.assertIn("FoundationStore &foundation_store()", source)
        self.assertIn("foundation_store().LoadHubBinding", source)
        self.assertIn(f"version: {FOUNDATION_SHA}", manifest)

    def test_trust_adapter_has_no_project_authority_or_laser_command_endpoint(self):
        text = "\n".join([
            (ROOT / "src" / "config_store.cpp").read_text(),
            (ROOT / "src" / "main.cpp").read_text(),
        ])
        self.assertNotIn("project_id", text)
        self.assertNotIn("LASER_SET_ON", text)
        self.assertNotIn("LASER_TOGGLE", text)

if __name__ == "__main__":
    unittest.main()
