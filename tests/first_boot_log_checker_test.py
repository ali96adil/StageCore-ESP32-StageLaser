from pathlib import Path
import importlib.util
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_first_boot_log", ROOT / "tools" / "check_first_boot_log.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


GOOD = """
I stagelaser: StageLaser firmware 0.1.0-dev.1 (abc123)
W stagelaser-relay: NO-ACTUATION build: relay GPIO is intentionally disabled
I stagelaser: laser truth restored; persisted=no resync_required=yes
I stagelaser: device_id=00000000-0000-0000-0000-000000000000
W stagelaser-setup: first-run provisioning AP SSID=StageLaser-000000 password=redacted
W stagelaser-setup: relay remains NO-ACTUATION; provisioning cannot assign a Project
"""


class FirstBootLogCheckerTest(unittest.TestCase):
    def test_good_no_actuation_first_boot(self):
        self.assertEqual(
            MODULE.evaluate(
                GOOD,
                expected_revision="abc123",
                expect_provisioning=True,
            ),
            [],
        )

    def test_rejects_actuation_build_marker(self):
        log = GOOD + (
            "\nE stagelaser-relay: actuation build requested but relay polarity "
            "is not qualified\n"
        )
        failures = MODULE.evaluate(log)
        self.assertTrue(any("actuation build requested" in item for item in failures))

    def test_rejects_safe_failure(self):
        failures = MODULE.evaluate(GOOD + "\nE stagelaser: safe failure: storage\n")
        self.assertTrue(any("safe failure" in item for item in failures))

    def test_revision_mismatch_fails(self):
        failures = MODULE.evaluate(GOOD, expected_revision="different")
        self.assertTrue(any("build revision mismatch" in item for item in failures))


if __name__ == "__main__":
    unittest.main()
