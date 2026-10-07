#include "firmware_update.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"

#ifndef STAGECORE_FW_VERSION
#define STAGECORE_FW_VERSION "0.1.0-dev.1"
#endif

namespace stagecore::stagelaser {
namespace {

constexpr char kTag[] = "stagelaser-ota";
constexpr char kProfileID[] = "stagecore.esp32-stagelaser";
constexpr char kQualification[] = "QUALIFIED";
constexpr char kArtifactPrefix[] =
    "/api/v1/stage-device-firmware/artifacts/";
constexpr int64_t kMaxArtifactBytes = 64LL << 20;
constexpr int kHTTPTimeoutMS = 15000;
constexpr size_t kReadChunk = 4096;

void set_failure(FirmwareUpdateFailure *failure,
                 const char *code,
                 const char *detail,
                 bool retryable) {
  if (failure == nullptr) return;
  failure->error_code = code != nullptr ? code : "FIRMWARE_UPDATE_FAILED";
  failure->detail = detail != nullptr ? detail : "Firmware update failed";
  failure->retryable = retryable;
}

bool exact_positive_integer(const cJSON *root, const char *key, int64_t *out) {
  if (root == nullptr || out == nullptr) return false;
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsNumber(value) || value->valuedouble < 1.0 ||
      value->valuedouble > 9007199254740991.0) {
    return false;
  }
  const int64_t exact = static_cast<int64_t>(value->valuedouble);
  if (static_cast<double>(exact) != value->valuedouble) return false;
  *out = exact;
  return true;
}

bool nonempty_string(const cJSON *root, const char *key, std::string *out) {
  if (root == nullptr || out == nullptr) return false;
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsString(item) || item->valuestring == nullptr ||
      item->valuestring[0] == '\0') {
    return false;
  }
  *out = item->valuestring;
  return true;
}

bool canonical_hex(const std::string &value, size_t expected_length) {
  if (value.size() != expected_length) return false;
  for (char ch : value) {
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool uuid_shape(const std::string &value) {
  if (value.size() != 36) return false;
  for (size_t i = 0; i < value.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (value[i] != '-') return false;
      continue;
    }
    const char ch = value[i];
    if (!((ch >= '0' && ch <= '9') ||
          (ch >= 'a' && ch <= 'f') ||
          (ch >= 'A' && ch <= 'F'))) {
      return false;
    }
  }
  return true;
}

bool valid_version(const std::string &value) {
  if (value.empty() || value.size() > 128) return false;
  for (unsigned char ch : value) {
    if (ch <= 0x20 || ch == 0x7f) return false;
  }
  return true;
}

bool canonical_artifact_path(const std::string &path) {
  if (path.rfind(kArtifactPrefix, 0) != 0) return false;
  if (path.find('\n') != std::string::npos ||
      path.find('\r') != std::string::npos ||
      path.find('\t') != std::string::npos ||
      path.find('\\') != std::string::npos ||
      path.find('?') != std::string::npos ||
      path.find('#') != std::string::npos ||
      path.find("..") != std::string::npos) {
    return false;
  }
  if (path.size() <= std::strlen(kArtifactPrefix) ||
      path.back() == '/') {
    return false;
  }
  return path.size() >= std::strlen("/firmware.bin") &&
         path.compare(path.size() - std::strlen("/firmware.bin"),
                      std::strlen("/firmware.bin"),
                      "/firmware.bin") == 0;
}

std::string sha256_hex(const unsigned char digest[32]) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(64);
  for (size_t i = 0; i < 32; ++i) {
    out[i * 2] = kHex[(digest[i] >> 4) & 0x0f];
    out[i * 2 + 1] = kHex[digest[i] & 0x0f];
  }
  return out;
}

esp_err_t emit(FirmwareProgressCallback progress,
               void *ctx,
               const char *state,
               const char *detail) {
  if (progress == nullptr) return ESP_OK;
  return progress(ctx, state, detail);
}

std::string artifact_url(const VerifiedHub &hub,
                         const FirmwareUpdateRequest &request) {
  char prefix[160] = {};
  std::snprintf(prefix, sizeof(prefix), "https://%s:%u",
                hub.address.c_str(), hub.port);
  return std::string(prefix) + request.artifact_path;
}

}  // namespace

esp_err_t parse_firmware_update_request(
    const std::string &frame,
    const std::string &expected_device_id,
    int64_t expected_connection_generation,
    FirmwareUpdateRequest *request,
    FirmwareUpdateFailure *failure) {
  if (request == nullptr || failure == nullptr ||
      expected_device_id.empty() || expected_connection_generation <= 0) {
    return ESP_ERR_INVALID_ARG;
  }
  *request = FirmwareUpdateRequest{};
  *failure = FirmwareUpdateFailure{};

  cJSON *root = cJSON_ParseWithLength(frame.data(), frame.size());
  if (root == nullptr) {
    set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                "Firmware maintenance frame is not valid JSON", false);
    return ESP_ERR_INVALID_RESPONSE;
  }

  esp_err_t result = ESP_ERR_INVALID_RESPONSE;
  do {
    std::string type;
    std::string device_id;
    int64_t generation = 0;
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    if (!nonempty_string(root, "type", &type) ||
        type != "maintenance.firmware_update" ||
        !cJSON_IsNumber(schema) || schema->valuedouble != 2.0 ||
        !nonempty_string(root, "device_id", &device_id) ||
        device_id != expected_device_id ||
        !exact_positive_integer(root, "connection_generation", &generation) ||
        generation != expected_connection_generation) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware maintenance envelope does not match this authenticated connection",
                  false);
      break;
    }

    const cJSON *update = cJSON_GetObjectItemCaseSensitive(root, "update");
    if (!cJSON_IsObject(update)) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware update manifest is missing", false);
      break;
    }

    const cJSON *manifest_schema =
        cJSON_GetObjectItemCaseSensitive(update, "schema_version");
    if (!cJSON_IsNumber(manifest_schema) ||
        manifest_schema->valuedouble != 1.0) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Unsupported firmware manifest schema", false);
      break;
    }

    request->connection_generation = generation;
    if (!nonempty_string(update, "update_id", &request->update_id) ||
        !uuid_shape(request->update_id)) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware update_id is invalid", false);
      break;
    }
    if (!nonempty_string(update, "device_id", &request->device_id) ||
        request->device_id != expected_device_id) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware manifest targets another device", false);
      break;
    }
    if (!nonempty_string(update, "profile_id", &request->profile_id) ||
        request->profile_id != kProfileID) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware manifest profile does not match StageLaser", false);
      break;
    }
    if (!nonempty_string(update, "current_version", &request->current_version) ||
        request->current_version != STAGECORE_FW_VERSION ||
        !valid_version(request->current_version)) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware manifest current_version does not match the running image",
                  false);
      break;
    }
    if (!nonempty_string(update, "target_version", &request->target_version) ||
        !valid_version(request->target_version) ||
        request->target_version == request->current_version) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware target_version is invalid", false);
      break;
    }
    if (!nonempty_string(update, "source_revision", &request->source_revision) ||
        !canonical_hex(request->source_revision, 40)) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware source revision is invalid", false);
      break;
    }

    std::string qualification;
    if (!nonempty_string(update, "qualification", &qualification) ||
        qualification != kQualification) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware artifact is not QUALIFIED", false);
      break;
    }
    if (!nonempty_string(update, "artifact_path", &request->artifact_path) ||
        !canonical_artifact_path(request->artifact_path)) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware artifact path is not a canonical Hub-local path",
                  false);
      break;
    }
    if (!exact_positive_integer(update, "artifact_size",
                                &request->artifact_size) ||
        request->artifact_size > kMaxArtifactBytes) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware artifact size is invalid", false);
      break;
    }
    if (!nonempty_string(update, "artifact_sha256",
                         &request->artifact_sha256) ||
        !canonical_hex(request->artifact_sha256, 64)) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware artifact SHA-256 is invalid", false);
      break;
    }

    const cJSON *rollback =
        cJSON_GetObjectItemCaseSensitive(update, "rollback_required");
    if (!cJSON_IsTrue(rollback)) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "StageLaser requires rollback protection", false);
      break;
    }
    request->rollback_required = true;

    std::string issued_at;
    std::string expires_at;
    if (!nonempty_string(update, "issued_at", &issued_at) ||
        !nonempty_string(update, "expires_at", &expires_at)) {
      set_failure(failure, "FIRMWARE_MANIFEST_REJECTED",
                  "Firmware manifest lifetime is missing", false);
      break;
    }

    result = ESP_OK;
  } while (false);

  cJSON_Delete(root);
  return result;
}

esp_err_t perform_firmware_update(
    const VerifiedHub &hub,
    const RuntimeCredential &credential,
    const FirmwareUpdateRequest &request,
    FirmwareProgressCallback progress,
    void *progress_ctx,
    FirmwareUpdateFailure *failure) {
  if (failure == nullptr || hub.address.empty() || hub.port == 0 ||
      hub.certificate_der.empty() || credential.token.empty() ||
      request.artifact_path.empty() || request.artifact_size <= 0) {
    return ESP_ERR_INVALID_ARG;
  }
  *failure = FirmwareUpdateFailure{};

  const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
  if (target == nullptr ||
      target->type != ESP_PARTITION_TYPE_APP ||
      request.artifact_size > static_cast<int64_t>(target->size)) {
    set_failure(failure, "FIRMWARE_OTA_LAYOUT_UNAVAILABLE",
                "No inactive OTA application slot can hold this firmware",
                false);
    return ESP_ERR_INVALID_SIZE;
  }

  if (emit(progress, progress_ctx, "DOWNLOADING",
           "Opening authenticated firmware artifact from the pinned Hub") !=
      ESP_OK) {
    set_failure(failure, "FIRMWARE_PROGRESS_SEND_FAILED",
                "Could not report firmware download state", true);
    return ESP_FAIL;
  }

  const std::string url = artifact_url(hub, request);
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = kHTTPTimeoutMS;
  config.cert_der =
      reinterpret_cast<const char *>(hub.certificate_der.data());
  config.cert_len = hub.certificate_der.size();
  config.skip_cert_common_name_check = true;
  config.tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_3;
  config.keep_alive_enable = true;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    set_failure(failure, "FIRMWARE_DOWNLOAD_FAILED",
                "Could not create authenticated firmware HTTP client", true);
    return ESP_ERR_NO_MEM;
  }

  esp_ota_handle_t ota_handle = 0;
  bool ota_active = false;
  bool sha_active = false;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);

  esp_err_t err = ESP_OK;
  const std::string authorization =
      "StageCoreSession " + credential.token;

  if (esp_http_client_set_header(
          client, "Authorization", authorization.c_str()) != ESP_OK ||
      esp_http_client_set_header(client, "Cache-Control", "no-store") != ESP_OK) {
    set_failure(failure, "FIRMWARE_DOWNLOAD_FAILED",
                "Could not configure authenticated firmware request", true);
    err = ESP_FAIL;
    goto cleanup;
  }

  err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    set_failure(failure, "FIRMWARE_DOWNLOAD_FAILED",
                "Could not open firmware artifact connection", true);
    goto cleanup;
  }

  {
    const int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
      set_failure(failure, "FIRMWARE_DOWNLOAD_FAILED",
                  "Firmware artifact headers could not be read", true);
      err = ESP_ERR_INVALID_RESPONSE;
      goto cleanup;
    }
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
      set_failure(failure, "FIRMWARE_DOWNLOAD_FAILED",
                  "Pinned Hub rejected the firmware artifact request", false);
      err = ESP_ERR_INVALID_RESPONSE;
      goto cleanup;
    }
    if (content_length > 0 &&
        content_length != request.artifact_size) {
      set_failure(failure, "FIRMWARE_INTEGRITY_FAILED",
                  "Firmware HTTP content length does not match the manifest",
                  false);
      err = ESP_ERR_INVALID_SIZE;
      goto cleanup;
    }
  }

  err = esp_ota_begin(
      target, static_cast<size_t>(request.artifact_size), &ota_handle);
  if (err != ESP_OK) {
    set_failure(failure, "FIRMWARE_OTA_WRITE_FAILED",
                "Could not begin writing the inactive OTA partition", false);
    goto cleanup;
  }
  ota_active = true;

  if (mbedtls_sha256_starts(&sha, 0) != 0) {
    set_failure(failure, "FIRMWARE_INTEGRITY_FAILED",
                "Could not initialize SHA-256 verification", false);
    err = ESP_FAIL;
    goto cleanup;
  }
  sha_active = true;

  if (emit(progress, progress_ctx, "WRITING",
           "Streaming firmware into the inactive OTA partition") != ESP_OK) {
    set_failure(failure, "FIRMWARE_PROGRESS_SEND_FAILED",
                "Could not report firmware write state", true);
    err = ESP_FAIL;
    goto cleanup;
  }

  {
    std::array<char, kReadChunk> buffer{};
    int64_t total = 0;
    while (total < request.artifact_size) {
      const int remaining =
          static_cast<int>(request.artifact_size - total > kReadChunk
                               ? kReadChunk
                               : request.artifact_size - total);
      const int read = esp_http_client_read(client, buffer.data(), remaining);
      if (read < 0) {
        set_failure(failure, "FIRMWARE_DOWNLOAD_FAILED",
                    "Firmware artifact stream failed before completion", true);
        err = ESP_FAIL;
        goto cleanup;
      }
      if (read == 0) {
        set_failure(failure, "FIRMWARE_DOWNLOAD_FAILED",
                    "Firmware artifact ended before the manifest size", true);
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
      }
      if (total + read > request.artifact_size) {
        set_failure(failure, "FIRMWARE_INTEGRITY_FAILED",
                    "Firmware artifact exceeded the manifest size", false);
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
      }
      if (mbedtls_sha256_update(
              &sha,
              reinterpret_cast<const unsigned char *>(buffer.data()),
              static_cast<size_t>(read)) != 0) {
        set_failure(failure, "FIRMWARE_INTEGRITY_FAILED",
                    "SHA-256 update failed while receiving firmware", false);
        err = ESP_FAIL;
        goto cleanup;
      }
      err = esp_ota_write(
          ota_handle, buffer.data(), static_cast<size_t>(read));
      if (err != ESP_OK) {
        set_failure(failure, "FIRMWARE_OTA_WRITE_FAILED",
                    "Writing the inactive OTA partition failed", false);
        goto cleanup;
      }
      total += read;
    }
    if (total != request.artifact_size) {
      set_failure(failure, "FIRMWARE_INTEGRITY_FAILED",
                  "Firmware byte count does not match the manifest", false);
      err = ESP_ERR_INVALID_SIZE;
      goto cleanup;
    }
  }

  if (emit(progress, progress_ctx, "VERIFYING",
           "Verifying complete firmware size, SHA-256 and ESP image") != ESP_OK) {
    set_failure(failure, "FIRMWARE_PROGRESS_SEND_FAILED",
                "Could not report firmware verification state", true);
    err = ESP_FAIL;
    goto cleanup;
  }

  {
    unsigned char digest[32] = {};
    if (mbedtls_sha256_finish(&sha, digest) != 0) {
      set_failure(failure, "FIRMWARE_INTEGRITY_FAILED",
                  "Could not finalize firmware SHA-256", false);
      err = ESP_FAIL;
      goto cleanup;
    }
    sha_active = false;
    if (sha256_hex(digest) != request.artifact_sha256) {
      set_failure(failure, "FIRMWARE_INTEGRITY_FAILED",
                  "Firmware SHA-256 does not match the qualified manifest",
                  false);
      err = ESP_ERR_INVALID_CRC;
      goto cleanup;
    }
  }

  err = esp_ota_end(ota_handle);
  ota_active = false;
  ota_handle = 0;
  if (err != ESP_OK) {
    set_failure(failure, "FIRMWARE_IMAGE_INVALID",
                "ESP-IDF rejected the completed OTA image", false);
    goto cleanup;
  }

  err = esp_ota_set_boot_partition(target);
  if (err != ESP_OK) {
    set_failure(failure, "FIRMWARE_BOOT_SELECTION_FAILED",
                "Verified OTA image could not be selected for next boot", false);
    goto cleanup;
  }

cleanup:
  if (sha_active) {
    unsigned char ignored[32] = {};
    (void)mbedtls_sha256_finish(&sha, ignored);
  }
  mbedtls_sha256_free(&sha);
  if (ota_active) {
    (void)esp_ota_abort(ota_handle);
  }
  (void)esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK) {
    ESP_LOGE(kTag, "firmware update failed: %s (%s)",
             failure->error_code.c_str(), failure->detail.c_str());
  } else {
    ESP_LOGI(kTag,
             "verified firmware staged in OTA partition %s for next boot",
             target->label);
  }
  return err;
}

}  // namespace stagecore::stagelaser
