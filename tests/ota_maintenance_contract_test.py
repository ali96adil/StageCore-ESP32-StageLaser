from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OTAMaintenanceContract(unittest.TestCase):
    def test_ota_capability_is_candidate_only(self):
        platformio = (ROOT / "platformio.ini").read_text()
        default = platformio.split("[env:esp32c3-ci-no-actuation]", 1)[1].split(
            "[env:esp32c3-ota-candidate-no-actuation]", 1
        )[0]
        candidate = platformio.split(
            "[env:esp32c3-ota-candidate-no-actuation]", 1
        )[1].split("[env:esp32c3-gpio3-no-load-qualification]", 1)[0]
        self.assertIn("-DSTAGECORE_OTA_ENABLED=0", default)
        self.assertIn("-DSTAGECORE_OTA_ENABLED=1", candidate)
        self.assertIn("-DSTAGECORE_LASER_ACTUATION_ENABLED=0", candidate)
        self.assertIn("-DSTAGECORE_LASER_OUTPUT_GPIO=-1", candidate)

    def test_runtime_keeps_firmware_maintenance_outside_show_commands(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        self.assertIn('"maintenance.firmware_update"', source)
        self.assertIn('"maintenance.firmware_update.result"', source)
        self.assertIn("context.commands_enabled = false", source)
        self.assertIn("known_safe_off(*laser)", source)
        self.assertNotIn("LASER_TOGGLE", source)

    def test_firmware_download_is_hub_local_and_pinned(self):
        source = (ROOT / "src" / "firmware_update.cpp").read_text()
        self.assertIn('constexpr char kArtifactPrefix[]', source)
        self.assertIn('"https://%s:%u"', source)
        self.assertIn("request.artifact_path", source)
        self.assertIn("hub.certificate_der", source)
        self.assertIn('"Authorization"', source)
        self.assertIn('"StageCoreSession "', source)
        self.assertIn("ESP_HTTP_CLIENT_TLS_VER_TLS_1_3", source)
        self.assertNotIn("http://", source)

    def test_streaming_write_is_verified_before_boot_selection(self):
        source = (ROOT / "src" / "firmware_update.cpp").read_text()
        write_pos = source.index("esp_ota_write(")
        verify_pos = source.index('"VERIFYING"')
        hash_compare_pos = source.index(
            "sha256_hex(digest) != request.artifact_sha256"
        )
        boot_pos = source.index("esp_ota_set_boot_partition")
        self.assertLess(write_pos, verify_pos)
        self.assertLess(verify_pos, hash_compare_pos)
        self.assertLess(hash_compare_pos, boot_pos)
        self.assertIn("esp_ota_get_next_update_partition", source)
        self.assertIn("esp_ota_abort", source)

    def test_rollback_required_and_safe_boot_confirmation_exist(self):
        update = (ROOT / "src" / "firmware_update.cpp").read_text()
        boot_guard = (ROOT / "src" / "ota_boot_guard.cpp").read_text()
        candidate = (ROOT / "sdkconfig.ota_candidate.defaults").read_text()
        self.assertIn('"rollback_required"', update)
        self.assertIn("cJSON_IsTrue(rollback)", update)
        self.assertIn("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y", candidate)
        self.assertIn("esp_ota_mark_app_valid_cancel_rollback", boot_guard)


if __name__ == "__main__":
    unittest.main()
