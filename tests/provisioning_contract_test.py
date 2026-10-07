from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ProvisioningContract(unittest.TestCase):
    def test_portal_has_no_project_assignment_surface(self):
        source = (ROOT / "src" / "provisioning.cpp").read_text()
        self.assertNotIn('name="project_id"', source)
        self.assertNotIn("Project ID", source)
        self.assertIn("Project and Runtime Snapshot assignment are owned by the", source)

    def test_setup_and_recovery_are_password_protected_and_non_actuating(self):
        source = (ROOT / "src" / "provisioning.cpp").read_text()
        store = (ROOT / "src" / "config_store.cpp").read_text()
        main = (ROOT / "src" / "main.cpp").read_text()

        self.assertIn("WIFI_AUTH_WPA2_PSK", source)
        self.assertIn('#define STAGECORE_SETUP_AP_PASSWORD "12345678"', source)
        self.assertIn("std::string effective_setup_ap_password()", source)
        self.assertIn('kSetupAPPasswordKey[] = "setup_ap_pass"', store)
        self.assertIn("save_setup_ap_password", store)
        self.assertIn("clear_setup_ap_password", store)
        self.assertIn("StageLaser-Recovery-", source)
        self.assertIn("WIFI_MODE_APSTA", source)
        self.assertIn("run_recovery_portal", main)
        self.assertIn("kRecoveryAfterFailed30sWindows = 3", main)
        self.assertNotIn("password=%s", source)
        self.assertNotIn("random_ap_password", source)
        self.assertNotIn("esp_random()", source)
        self.assertNotIn("relay_pick(", source)
        self.assertNotIn("relay_release(", source)

    def test_setup_ap_maintenance_is_v2_authenticated_and_non_actuating(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        self.assertIn("device.maintenance.setup-ap-password", source)
        self.assertIn("maintenance.setup_ap_password", source)
        self.assertIn("maintenance.setup_ap_password.result", source)
        self.assertIn("connection_generation", source)
        self.assertIn("save_setup_ap_password", source)
        self.assertIn("clear_setup_ap_password", source)
        self.assertNotIn('cJSON_AddStringToObject(root, "password"', source)

        start = source.index("esp_err_t process_setup_ap_maintenance")
        end = source.index("bool known_safe_off", start)
        maintenance = source[start:end]
        for forbidden in (
            "SafeOff(",
            "laser->",
            "relay_pick(",
            "relay_release(",
            "pulse",
        ):
            self.assertNotIn(forbidden, maintenance)

if __name__ == "__main__":
    unittest.main()
