from pathlib import Path
import csv
import unittest

ROOT = Path(__file__).resolve().parents[1]
FLASH_SIZE = 0x400000


def parse_int(value: str) -> int:
    return int(value.strip(), 0)


class OTAPartitionCandidateContract(unittest.TestCase):
    def setUp(self):
        path = ROOT / "partitions-ota-candidate-4mb.csv"
        rows = []
        with path.open(newline="") as fh:
            for raw in fh:
                if not raw.strip() or raw.lstrip().startswith("#"):
                    continue
                row = next(csv.reader([raw]))
                rows.append([field.strip() for field in row])
        self.rows = {
            row[0]: {
                "type": row[1],
                "subtype": row[2],
                "offset": parse_int(row[3]),
                "size": parse_int(row[4]),
            }
            for row in rows
        }

    def test_has_required_dual_ota_layout(self):
        self.assertNotIn("factory", self.rows)
        self.assertEqual(self.rows["otadata"]["type"], "data")
        self.assertEqual(self.rows["otadata"]["subtype"], "ota")
        self.assertEqual(self.rows["otadata"]["size"], 0x2000)
        self.assertEqual(self.rows["ota_0"]["subtype"], "ota_0")
        self.assertEqual(self.rows["ota_1"]["subtype"], "ota_1")

    def test_app_slots_are_equal_and_aligned(self):
        ota0 = self.rows["ota_0"]
        ota1 = self.rows["ota_1"]
        self.assertEqual(ota0["size"], 0x1A0000)
        self.assertEqual(ota1["size"], ota0["size"])
        self.assertEqual(ota0["offset"] % 0x10000, 0)
        self.assertEqual(ota1["offset"] % 0x10000, 0)
        self.assertEqual(ota1["offset"], ota0["offset"] + ota0["size"])

    def test_layout_fits_exactly_in_verified_4mb_flash(self):
        spans = sorted(
            (entry["offset"], entry["offset"] + entry["size"], name)
            for name, entry in self.rows.items()
        )
        for (_, end, name), (next_start, _, next_name) in zip(spans, spans[1:]):
            self.assertLessEqual(
                end, next_start, f"{name} overlaps {next_name}"
            )
        self.assertEqual(self.rows["storage"]["offset"] + self.rows["storage"]["size"], FLASH_SIZE)

    def test_candidate_is_not_default_and_is_no_actuation(self):
        ini = (ROOT / "platformio.ini").read_text()
        self.assertIn("default_envs = esp32c3-ci-no-actuation", ini)
        self.assertIn("[env:esp32c3-ota-candidate-no-actuation]", ini)
        self.assertIn("partitions-ota-candidate-4mb.csv", ini)
        candidate = ini.split("[env:esp32c3-ota-candidate-no-actuation]", 1)[1]
        self.assertIn("STAGECORE_LASER_ACTUATION_ENABLED=0", candidate)
        self.assertIn("STAGECORE_LASER_OUTPUT_GPIO=-1", candidate)
        self.assertIn("STAGECORE_LASER_SHARED_POWER_QUALIFIED=0", candidate)


if __name__ == "__main__":
    unittest.main()
