import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import check_ota_candidate as ota


class OtaCandidateContractTest(unittest.TestCase):
    def test_repository_candidate_is_valid(self):
        partitions = ota.load_partitions(ROOT / "partitions-ota-candidate-4mb.csv")
        slot_size = ota.validate_layout(partitions)
        self.assertEqual(slot_size, 0x1A0000)

    def test_overlap_is_rejected(self):
        partitions = ota.load_partitions(ROOT / "partitions-ota-candidate-4mb.csv")
        modified = [dict(partition) for partition in partitions]
        by_name = {partition["name"]: partition for partition in modified}
        by_name["ota_1"]["offset"] = by_name["ota_0"]["offset"] + 0x10000
        with self.assertRaisesRegex(ValueError, "overlaps"):
            ota.validate_layout(modified)

    def test_missing_second_slot_is_rejected(self):
        partitions = [
            partition
            for partition in ota.load_partitions(
                ROOT / "partitions-ota-candidate-4mb.csv"
            )
            if partition["name"] != "ota_1"
        ]
        with self.assertRaisesRegex(ValueError, "missing required partitions"):
            ota.validate_layout(partitions)

    def test_unequal_slots_are_rejected(self):
        partitions = ota.load_partitions(ROOT / "partitions-ota-candidate-4mb.csv")
        modified = [dict(partition) for partition in partitions]
        for partition in modified:
            if partition["name"] == "ota_1":
                partition["size"] -= 0x10000
        with self.assertRaisesRegex(ValueError, "equal size"):
            ota.validate_layout(modified)

    def test_oversized_firmware_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "firmware.bin"
            path.write_bytes(b"x" * 17)
            self.assertEqual(ota.validate_firmware(path, 17), 17)
            with self.assertRaisesRegex(ValueError, "OTA slot"):
                ota.validate_firmware(path, 16)


if __name__ == "__main__":
    unittest.main()
