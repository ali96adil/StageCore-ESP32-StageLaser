from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class HubTrustContract(unittest.TestCase):
    def test_discovery_is_existing_hub_service_with_tls_pin(self):
        source = (ROOT / "src" / "hub_discovery.cpp").read_text()
        self.assertIn('_stagecore-hub', source)
        self.assertIn('ESP_TLS_VER_TLS_1_3', source)
        self.assertIn('tls_sha256', source)
        self.assertIn('TLS pin mismatch', source)

    def test_pairing_advertises_only_stagelaser_capabilities(self):
        source = (ROOT / "src" / "hub_security.cpp").read_text()
        for capability in [
            "laser.arm", "laser.disarm", "laser.state.set",
            "laser.flash.start", "laser.flash.stop", "laser.safe_off",
            "laser.state.read", "laser.state.resync",
        ]:
            self.assertIn(capability, source)
        self.assertNotIn("lighting.", source)
        self.assertIn('"architecture", "riscv32"', source)

    def test_trust_path_has_no_project_authority_or_laser_command_endpoint(self):
        text = "\n".join([
            (ROOT / "src" / "hub_discovery.cpp").read_text(),
            (ROOT / "src" / "hub_security.cpp").read_text(),
        ])
        self.assertNotIn("project_id", text)
        self.assertNotIn("LASER_SET_ON", text)
        self.assertNotIn("LASER_TOGGLE", text)

if __name__ == "__main__":
    unittest.main()
