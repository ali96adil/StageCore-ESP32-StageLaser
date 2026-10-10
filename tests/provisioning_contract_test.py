from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ProvisioningContract(unittest.TestCase):
    def test_portal_has_no_project_assignment_surface(self):
        source = (ROOT / "src" / "provisioning.cpp").read_text()
        self.assertNotIn('name="project_id"', source)
        self.assertNotIn("Project ID", source)
        self.assertIn("Project and Runtime Snapshot assignment are owned by the", source)

    def test_setup_and_recovery_use_shared_foundation_policy_and_are_non_actuating(self):
        source = (ROOT / "src" / "provisioning.cpp").read_text()
        store = (ROOT / "src" / "config_store.cpp").read_text()
        main = (ROOT / "src" / "main.cpp").read_text()
        manifest = (ROOT / "src" / "idf_component.yml").read_text()

        self.assertIn("WIFI_AUTH_WPA2_PSK", source)
        self.assertIn('#include "foundation_contract.h"', source)
        self.assertIn("kDefaultSetupAPPassword", source)
        self.assertIn("foundation_store().EffectiveSetupAPPassword", source)
        self.assertIn("foundation_store().SaveSetupAPPasswordOverride", store)
        self.assertIn("foundation_store().ResetSetupAPPasswordToDefault", store)
        self.assertIn("stagecore_foundation:", manifest)
        self.assertIn("https://github.com/ali96adil/StageCore.git", manifest)
        self.assertIn(
            "version: 12be83968efff876d8cb239b79368b3dafbf044b",
            manifest,
        )
        self.assertIn("StageLaser-Recovery-", source)
        self.assertIn("WIFI_MODE_APSTA", source)
        self.assertIn("run_recovery_portal", main)
        self.assertIn("kRecoveryAfterFailed30sWindows = 3", main)
        self.assertNotIn("STAGECORE_SETUP_AP_PASSWORD", source)
        self.assertNotIn("password=%s", source)
        self.assertNotIn("random_ap_password", source)
        self.assertNotIn("esp_random()", source)
        self.assertNotIn("relay_pick(", source)
        self.assertNotIn("relay_release(", source)

    def test_setup_ap_maintenance_is_v2_authenticated_and_non_actuating(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        store = (ROOT / "src" / "config_store.cpp").read_text()
        self.assertIn("device.maintenance.setup-ap-password", source)
        self.assertIn("maintenance.setup_ap_password", source)
        self.assertIn("maintenance.setup_ap_password.result", source)
        self.assertIn("connection_generation", source)
        self.assertIn("save_setup_ap_password", source)
        self.assertIn("clear_setup_ap_password", source)
        self.assertIn("SaveSetupAPPasswordOverride", store)
        self.assertIn("ResetSetupAPPasswordToDefault", store)
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

    def test_local_foundation_duplicates_are_removed(self):
        for name in (
            "device_identity.cpp",
            "device_identity.h",
            "hub_discovery.cpp",
            "hub_discovery.h",
            "hub_security.cpp",
            "hub_security.h",
            "trusted_clock.cpp",
            "trusted_clock.h",
        ):
            self.assertFalse((ROOT / "src" / name).exists(), name)

if __name__ == "__main__":
    unittest.main()
