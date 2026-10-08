from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ObservationContract(unittest.TestCase):
    def test_observation_matches_stagecore_contract(self):
        source = (ROOT / "src" / "laser_observation.cpp").read_text()
        for key in [
            "schema_version", "firmware_version", "build_revision",
            "actuation_enabled", "shared_power_qualified",
            "gpio_no_load_qualification", "output_gpio",
            "control_contract_version", "boot_id", "uptime_seconds",
            "reset_reason", "reset_reason_code", "wifi_rssi_dbm",
            "ip_address", "arm_state", "logical_state", "state_quality",
            "resync_required", "pulse_in_progress", "relay_pulse_count",
            "driver_kind", "limits", "active_flash",
            "last_accepted_command_id", "last_applied_command_id",
            "last_command_type", "last_command_result",
        ]:
            self.assertIn(f'"{key}"', source)

    def test_runtime_populates_boot_id_and_diagnostic_ip(self):
        runtime = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        self.assertIn("metadata.boot_id = boot_id();", runtime)
        self.assertIn("static const std::string id = make_boot_id();", runtime)
        self.assertIn('esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")', runtime)
        self.assertIn("esp_netif_get_ip_info", runtime)
        self.assertIn("metadata.ip_address = station_ip_address();", runtime)

    def test_build_provenance_and_reset_code_are_populated_not_guessed(self):
        runtime = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        self.assertIn("metadata.build_revision = STAGECORE_BUILD_REVISION;", runtime)
        self.assertIn("metadata.reset_reason_code = static_cast<int>(esp_reset_reason());", runtime)
        self.assertIn("metadata.actuation_enabled = STAGECORE_LASER_ACTUATION_ENABLED == 1;", runtime)
        self.assertIn("metadata.shared_power_qualified = STAGECORE_LASER_SHARED_POWER_QUALIFIED == 1;", runtime)
        self.assertIn("metadata.gpio_no_load_qualification = STAGECORE_GPIO_NO_LOAD_QUALIFICATION == 1;", runtime)
        self.assertIn("metadata.output_gpio = STAGECORE_LASER_OUTPUT_GPIO;", runtime)
        observation = (ROOT / "src" / "laser_observation.cpp").read_text()
        self.assertIn("metadata.reset_reason_code", observation)
        self.assertIn("metadata.build_revision", observation)
        # No change to laser state machine or actuation in this diagnostic patch.
        self.assertIn('"resync_required", machine.resync_required()', observation)

    def test_v1_driver_is_tracked_not_sensor_confirmed(self):
        source = (ROOT / "src" / "laser_observation.cpp").read_text()
        self.assertIn('"MECHANICAL_RELAY"', source)
        self.assertNotIn('"state_quality", "CONFIRMED"', source)

if __name__ == "__main__":
    unittest.main()
