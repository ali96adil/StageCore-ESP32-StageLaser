from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class IdentityConfigContract(unittest.TestCase):
    def test_v2_config_has_no_project_authority(self):
        header = (ROOT / "src" / "config_store.h").read_text()
        source = (ROOT / "src" / "config_store.cpp").read_text()
        self.assertNotIn("project_id", header)
        self.assertNotIn("kProjectKey", source)

    def test_persistent_p256_identity_is_present(self):
        source = (ROOT / "src" / "device_identity.cpp").read_text()
        self.assertIn("MBEDTLS_ECP_DP_SECP256R1", source)
        self.assertIn("stagecore_id", source)
        self.assertIn("ec_priv_der", source)

if __name__ == "__main__":
    unittest.main()
