#include "command_replay_store.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "mbedtls/sha256.h"
#include "nvs.h"

namespace stagecore::stagelaser {
namespace {

constexpr char kNamespace[] = "laser_cmd";
constexpr char kBlobKey[] = "seen_v1";
constexpr char kEmergencyWatermarkKey[] = "off_ts_v1";
constexpr char kControlGenerationKey[] = "max_gen_v1";
constexpr uint64_t kMaximumExactGeneration = 9007199254740991ULL;
constexpr uint32_t kMagic = 0x534C434D;  // "SLCM"
constexpr uint8_t kVersion = 1;
constexpr size_t kDigestBytes = 32;

struct ReplayBlob {
  uint32_t magic = kMagic;
  uint8_t version = kVersion;
  uint8_t count = 0;
  uint8_t next = 0;
  uint8_t reserved = 0;
  uint8_t digests[kPersistentCommandReplayCapacity][kDigestBytes]{};
};

static_assert(sizeof(ReplayBlob) <= 1200,
              "StageLaser replay fence must remain a small NVS blob");

esp_err_t digest_id(const std::string &command_id,
                    std::array<uint8_t, kDigestBytes> *digest) {
  if (command_id.empty() || command_id.size() > 128 || digest == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  const int rc = mbedtls_sha256(
      reinterpret_cast<const unsigned char *>(command_id.data()),
      command_id.size(), digest->data(), 0);
  return rc == 0 ? ESP_OK : ESP_FAIL;
}

bool valid_blob(const ReplayBlob &blob) {
  return blob.magic == kMagic && blob.version == kVersion &&
         blob.count <= kPersistentCommandReplayCapacity &&
         blob.next < kPersistentCommandReplayCapacity;
}

esp_err_t load_blob(ReplayBlob *blob) {
  if (blob == nullptr) return ESP_ERR_INVALID_ARG;
  *blob = ReplayBlob{};

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;

  size_t size = 0;
  err = nvs_get_blob(handle, kBlobKey, nullptr, &size);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return ESP_OK;
  }
  if (err != ESP_OK || size != sizeof(ReplayBlob)) {
    nvs_close(handle);
    return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
  }

  err = nvs_get_blob(handle, kBlobKey, blob, &size);
  nvs_close(handle);
  if (err != ESP_OK) return err;
  return valid_blob(*blob) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool contains(const ReplayBlob &blob,
              const std::array<uint8_t, kDigestBytes> &digest) {
  for (size_t i = 0; i < blob.count; ++i) {
    if (std::memcmp(blob.digests[i], digest.data(), kDigestBytes) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

esp_err_t command_replay_seen(const std::string &command_id, bool *seen) {
  if (seen == nullptr) return ESP_ERR_INVALID_ARG;
  *seen = false;

  std::array<uint8_t, kDigestBytes> digest{};
  esp_err_t err = digest_id(command_id, &digest);
  if (err != ESP_OK) return err;

  ReplayBlob blob;
  err = load_blob(&blob);
  if (err != ESP_OK) return err;
  *seen = contains(blob, digest);
  return ESP_OK;
}

esp_err_t command_replay_remember(const std::string &command_id) {
  std::array<uint8_t, kDigestBytes> digest{};
  esp_err_t err = digest_id(command_id, &digest);
  if (err != ESP_OK) return err;

  ReplayBlob blob;
  err = load_blob(&blob);
  if (err != ESP_OK) return err;
  if (contains(blob, digest)) return ESP_OK;

  const size_t slot = blob.count < kPersistentCommandReplayCapacity
                          ? blob.count
                          : blob.next;
  std::memcpy(blob.digests[slot], digest.data(), kDigestBytes);

  if (blob.count < kPersistentCommandReplayCapacity) {
    ++blob.count;
    blob.next = static_cast<uint8_t>(
        blob.count % kPersistentCommandReplayCapacity);
  } else {
    blob.next = static_cast<uint8_t>(
        (blob.next + 1) % kPersistentCommandReplayCapacity);
  }

  nvs_handle_t handle;
  err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = nvs_set_blob(handle, kBlobKey, &blob, sizeof(blob));
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t command_control_generation_load(uint64_t *generation) {
  if (generation == nullptr) return ESP_ERR_INVALID_ARG;
  *generation = 0;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;
  uint64_t persisted = 0;
  err = nvs_get_u64(handle, kControlGenerationKey, &persisted);
  nvs_close(handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;
  if (persisted == 0 || persisted > kMaximumExactGeneration)
    return ESP_ERR_INVALID_STATE;
  *generation = persisted;
  return ESP_OK;
}

esp_err_t command_control_generation_remember(uint64_t generation) {
  if (generation == 0 || generation > kMaximumExactGeneration)
    return ESP_ERR_INVALID_ARG;
  uint64_t previous = 0;
  esp_err_t err = command_control_generation_load(&previous);
  if (err != ESP_OK) return err;
  if (generation <= previous) return ESP_OK;  // Never rewind.

  nvs_handle_t handle;
  err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = nvs_set_u64(handle, kControlGenerationKey, generation);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t command_emergency_watermark_load(int64_t *issued_at_unix_ms) {
  if (issued_at_unix_ms == nullptr) return ESP_ERR_INVALID_ARG;
  *issued_at_unix_ms = 0;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;
  int64_t value = 0;
  err = nvs_get_i64(handle, kEmergencyWatermarkKey, &value);
  nvs_close(handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;
  if (value <= 0) return ESP_ERR_INVALID_STATE;
  *issued_at_unix_ms = value;
  return ESP_OK;
}

esp_err_t command_emergency_watermark_remember(int64_t issued_at_unix_ms) {
  if (issued_at_unix_ms <= 0) return ESP_ERR_INVALID_ARG;
  int64_t previous = 0;
  esp_err_t err = command_emergency_watermark_load(&previous);
  if (err != ESP_OK) return err;
  if (issued_at_unix_ms <= previous) return ESP_OK;

  nvs_handle_t handle;
  err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = nvs_set_i64(handle, kEmergencyWatermarkKey, issued_at_unix_ms);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

}  // namespace stagecore::stagelaser
