from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EmergencyQueueNativeTests(unittest.TestCase):
    def test_actual_queue_policy_compiles_and_handles_priority_and_supersession(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "emergency_queue_test"
            subprocess.run(
                [
                    "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-Isrc", "tests/emergency_command_queue_native_test.cpp",
                    "-o", str(binary),
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
