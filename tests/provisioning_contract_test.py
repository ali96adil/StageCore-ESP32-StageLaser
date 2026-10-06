from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ProvisioningContract(unittest.TestCase):
    def test_portal_has_no_project_assignment_surface(self):
        source = (ROOT / "src" / "provisioning.cpp").read_text()
        self.assertNotIn('name="project_id"', source)
        self.assertNotIn("Project ID", source)
        self.assertIn("Project and Runtime Snapshot assignment are owned by the", source)

    def test_portal_is_password_protected_and_laser_safe(self):
        source = (ROOT / "src" / "provisioning.cpp").read_text()
        self.assertIn("WIFI_AUTH_WPA2_PSK", source)
        self.assertIn("random_ap_password", source)
        self.assertIn("Laser output remains disabled and", source)
        self.assertNotIn("relay_pick(", source)
        self.assertNotIn("relay_release(", source)

if __name__ == "__main__":
    unittest.main()
