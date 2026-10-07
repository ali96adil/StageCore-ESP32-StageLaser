import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ExactSourceCiContractTest(unittest.TestCase):
    def test_all_jobs_use_the_same_expected_head(self):
        workflow = (
            ROOT / ".github" / "workflows" / "firmware-ci.yml"
        ).read_text()

        self.assertIn(
            "EXPECTED_SHA: ${{ github.event.pull_request.head.sha || github.sha }}",
            workflow,
        )
        self.assertGreaterEqual(
            workflow.count("ref: ${{ env.EXPECTED_SHA }}"),
            2,
        )
        self.assertGreaterEqual(
            workflow.count('test "$(git rev-parse HEAD)" = "$EXPECTED_SHA"'),
            2,
        )
        self.assertGreaterEqual(
            workflow.count(
                'test -z "$(git status --porcelain --untracked-files=all)"'
            ),
            2,
        )

    def test_unqualified_artifact_is_revision_stamped(self):
        workflow = (
            ROOT / ".github" / "workflows" / "firmware-ci.yml"
        ).read_text()

        self.assertIn(
            "Stage exact unqualified firmware artifacts",
            workflow,
        )
        self.assertIn(
            'strings "$SRC" | grep -F "$EXPECTED_SHA"',
            workflow,
        )
        self.assertIn(
            "name: stagecore-stagelaser-unqualified",
            workflow,
        )
        self.assertIn(
            'echo "qualification=UNQUALIFIED"',
            workflow,
        )
        self.assertIn(
            'echo "actuation_enabled=0"',
            workflow,
        )


if __name__ == "__main__":
    unittest.main()
