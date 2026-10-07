#pragma once

#include <cstdint>
#include <string>

#include "esp_err.h"
#include "hub_discovery.h"
#include "hub_security.h"

namespace stagecore::stagelaser {

struct FirmwareUpdateRequest {
  std::string update_id;
  std::string device_id;
  std::string profile_id;
  std::string current_version;
  std::string target_version;
  std::string source_revision;
  std::string artifact_path;
  std::string artifact_sha256;
  int64_t artifact_size = 0;
  int64_t connection_generation = 0;
  bool rollback_required = false;
};

struct FirmwareUpdateFailure {
  std::string error_code;
  std::string detail;
  bool retryable = false;
};

using FirmwareProgressCallback =
    esp_err_t (*)(void *ctx, const char *state, const char *detail);

esp_err_t parse_firmware_update_request(
    const std::string &frame,
    const std::string &expected_device_id,
    int64_t expected_connection_generation,
    FirmwareUpdateRequest *request,
    FirmwareUpdateFailure *failure);

esp_err_t perform_firmware_update(
    const VerifiedHub &hub,
    const RuntimeCredential &credential,
    const FirmwareUpdateRequest &request,
    FirmwareProgressCallback progress,
    void *progress_ctx,
    FirmwareUpdateFailure *failure);

}  // namespace stagecore::stagelaser
