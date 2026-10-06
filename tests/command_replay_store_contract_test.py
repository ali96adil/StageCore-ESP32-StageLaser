from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class PersistentCommandReplayContract(unittest.TestCase):
    def test_replay_fence_is_bounded_and_hashed(self):
        header = (ROOT / "src" / "command_replay_store.h").read_text()
        source = (ROOT / "src" / "command_replay_store.cpp").read_text()
        self.assertIn("kPersistentCommandReplayCapacity = 32", header)
        self.assertIn("mbedtls_sha256", source)
        self.assertIn("nvs_set_blob", source)
        self.assertIn("nvs_commit", source)
        self.assertIn("seen_v1", source)

    def test_storage_failure_is_not_treated_as_a_cache_miss(self):
        source = (ROOT / "src" / "command_replay_store.cpp").read_text()
        self.assertIn("return valid_blob(*blob) ? ESP_OK : ESP_ERR_INVALID_STATE", source)
        self.assertNotIn("LASER_TOGGLE", source)

if __name__ == "__main__":
    unittest.main()
