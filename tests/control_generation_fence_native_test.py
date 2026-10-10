from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ControlGenerationFenceTests(unittest.TestCase):
    def test_native_monotonic_fence_handles_reordered_go_and_emergency_off(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "control_generation_test"
            subprocess.run(
                [
                    "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-Isrc", "tests/control_generation_fence_native_test.cpp",
                    "-o", str(binary),
                ],
                cwd=ROOT, check=True, capture_output=True, text=True,
            )
            subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=5)

    def test_strict_command_envelope_requires_exact_integer_generation(self):
        source = (ROOT / "src" / "laser_command_contract.cpp").read_text()
        self.assertIn('"control_generation"', source)
        self.assertIn("positive_control_generation(command, &parsed.control_generation)", source)
        self.assertIn("9007199254740991.0", source)

    def test_persistent_nvs_generation_loaded_before_any_command(self):
        source = (ROOT / "src" / "stage_device_runtime.cpp").read_text()
        boot = source.split("esp_err_t run_stage_device_runtime(", 1)[1]
        self.assertIn("command_control_generation_load(", boot)
        self.assertLess(boot.index("command_control_generation_load("),
                        boot.index("esp_websocket_client_start(client)"))
        processing = source.split("std::string command_frame;", 1)[1].split(
            "const uint64_t now_ms =", 1)[0]
        self.assertIn("DecideControlGeneration(", processing)
        self.assertIn("command_control_generation_remember(candidate)", processing)
        self.assertLess(processing.index("command_control_generation_remember(candidate)"),
                        processing.index("start_ready_command("))
        self.assertIn("generation_superseded", processing)
        self.assertIn('"DEVICE_COMMAND_SUPERSEDED"', processing)
        store = (ROOT / "src" / "command_replay_store.cpp").read_text()
        self.assertIn('kControlGenerationKey[] = "max_gen_v1"', store)
        self.assertIn("nvs_set_u64(handle, kControlGenerationKey, generation)", store)
        self.assertIn("nvs_commit(handle)", store)


if __name__ == "__main__":
    unittest.main()
