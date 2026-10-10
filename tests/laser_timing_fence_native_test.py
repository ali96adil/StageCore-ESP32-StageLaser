from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LaserTimingFenceNativeTests(unittest.TestCase):
    def test_actual_timing_fence_compiles_and_rejects_stale_enabling_authority(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "laser_timing_fence_test"
            subprocess.run(
                [
                    "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-Isrc", "tests/laser_timing_fence_native_test.cpp",
                    "-o", str(binary),
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=5)

    def test_firmware_uses_native_timing_fence_before_enabling_commands(self):
        source = (ROOT / "src" / "laser_command_contract.cpp").read_text()
        self.assertIn('#include "laser_timing_fence.h"', source)
        self.assertIn("RequiresFreshDeadline(command_type, resync_to_on)", source)
        self.assertIn("DEVICE_COMMAND_DEADLINE_REQUIRED", source)
        self.assertIn("DEVICE_COMMAND_ISSUED_IN_FUTURE", source)
        self.assertIn("IssuedTooFarInFuture(parsed.issued_at_unix_ms, now_ms)", source)


if __name__ == "__main__":
    unittest.main()
