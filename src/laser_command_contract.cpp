#include "laser_command_contract.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "cJSON.h"
#include "laser_contract.h"
#include "laser_timing_fence.h"
#include "trusted_clock.h"

namespace stagecore::stagelaser {
namespace {

constexpr int kOuterSchemaVersion = 2;
constexpr int kCommandSchemaVersion = 1;
constexpr size_t kMaxID = 128;
constexpr size_t kMaxCommandType = 64;
constexpr size_t kMaxIssuer = 128;
constexpr size_t kMaxPriority = 32;
constexpr size_t kMaxIdempotency = 256;
constexpr size_t kMaxPayloadBytes = 1024;

bool string_field(const cJSON *object, const char *key,
                  std::string *value, bool required,
                  size_t max_length) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (item == nullptr) {
    if (required) return false;
    value->clear();
    return true;
  }
  if (!cJSON_IsString(item) || item->valuestring == nullptr) return false;
  const size_t n = std::strlen(item->valuestring);
  if ((required && n == 0) || n > max_length) return false;
  *value = item->valuestring;
  return true;
}

bool number_field(const cJSON *object, const char *key, int *value) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
      item->valuedouble != std::floor(item->valuedouble) ||
      item->valuedouble < static_cast<double>(std::numeric_limits<int>::min()) ||
      item->valuedouble > static_cast<double>(std::numeric_limits<int>::max())) {
    return false;
  }
  *value = static_cast<int>(item->valuedouble);
  return true;
}

bool positive_control_generation(const cJSON *object, uint64_t *value) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(
      object, "control_generation");
  // JSON/cJSON numbers are doubles: stay in the exact integer range to
  // prevent generation aliasing or truncation at the ESP32 boundary.
  constexpr double kMaxExactGeneration = 9007199254740991.0;
  if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
      item->valuedouble < 1.0 ||
      item->valuedouble > kMaxExactGeneration ||
      item->valuedouble != std::floor(item->valuedouble) ||
      value == nullptr) return false;
  *value = static_cast<uint64_t>(item->valuedouble);
  return true;
}

bool allowed_root_key(const char *key) {
  return std::strcmp(key, "type") == 0 ||
         std::strcmp(key, "schema_version") == 0 ||
         std::strcmp(key, "device_id") == 0 ||
         std::strcmp(key, "command") == 0;
}

bool allowed_command_key(const char *key) {
  static constexpr const char *kAllowed[] = {
      "command_id", "command_type", "schema_version", "issued_at",
      "deadline_at", "project_id", "runtime_snapshot_id", "issuer",
      "correlation_id", "causation_id", "priority", "idempotency_key",
      "control_generation", "payload",
  };
  for (const char *candidate : kAllowed) {
    if (std::strcmp(key, candidate) == 0) return true;
  }
  return false;
}

bool no_unknown_fields(const cJSON *object,
                       bool (*allowed)(const char *)) {
  if (!cJSON_IsObject(object) || allowed == nullptr) return false;
  const cJSON *child = nullptr;
  cJSON_ArrayForEach(child, object) {
    if (child->string == nullptr || !allowed(child->string)) return false;
    // JSON object keys must be unique. Otherwise different parsers can
    // disagree about the command ID, scope, timestamp or payload.
    for (const cJSON *next = child->next; next != nullptr; next = next->next) {
      if (next->string == nullptr ||
          std::strcmp(child->string, next->string) == 0) return false;
    }
  }
  return true;
}

bool supported_command_type(const std::string &type) {
  static constexpr const char *kTypes[] = {
      "LASER_ARM",
      "LASER_DISARM",
      "LASER_SET_ON",
      "LASER_SET_OFF",
      "LASER_FLASH_START",
      "LASER_FLASH_STOP",
      "LASER_SAFE_OFF",
      "LASER_STATE_READ",
      "LASER_STATE_RESYNC",
  };
  for (const char *candidate : kTypes) {
    if (type == candidate) return true;
  }
  return false;
}

bool payload_has_only(const cJSON *payload,
                      const char *first,
                      const char *second = nullptr) {
  if (!cJSON_IsObject(payload)) return false;
  int count = 0;
  const cJSON *child = nullptr;
  cJSON_ArrayForEach(child, payload) {
    ++count;
    if (child->string == nullptr) return false;
    const bool allowed =
        std::strcmp(child->string, first) == 0 ||
        (second != nullptr && std::strcmp(child->string, second) == 0);
    if (!allowed) return false;
  }
  return count == (second == nullptr ? 1 : 2);
}

bool empty_payload(const cJSON *payload) {
  return cJSON_IsObject(payload) && cJSON_GetArraySize(payload) == 0;
}

// Output-enabling commands cannot remain valid indefinitely after a delayed
// WebSocket frame or a Hub/device reconnection. STOP/SAFE_OFF and OFF commands
// are deliberately exempt so a clock outage does not prevent safe actions.
bool requires_fresh_deadline(const std::string &command_type,
                             const cJSON *payload) {
  bool resync_to_on = false;
  if (command_type == "LASER_STATE_RESYNC") {
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(payload, "state");
    resync_to_on = cJSON_IsString(state) && state->valuestring != nullptr &&
                   std::strcmp(state->valuestring, "ON") == 0;
  }
  return RequiresFreshDeadline(command_type, resync_to_on);
}

bool valid_payload(const std::string &command_type,
                   const cJSON *payload) {
  if (!cJSON_IsObject(payload)) return false;

  if (command_type == "LASER_ARM" ||
      command_type == "LASER_DISARM" ||
      command_type == "LASER_SET_ON" ||
      command_type == "LASER_SET_OFF" ||
      command_type == "LASER_FLASH_STOP" ||
      command_type == "LASER_SAFE_OFF" ||
      command_type == "LASER_STATE_READ") {
    return empty_payload(payload);
  }

  if (command_type == "LASER_FLASH_START") {
    if (!payload_has_only(payload, "frequency_hz", "duration_ms")) {
      return false;
    }
    const cJSON *frequency =
        cJSON_GetObjectItemCaseSensitive(payload, "frequency_hz");
    const cJSON *duration =
        cJSON_GetObjectItemCaseSensitive(payload, "duration_ms");
    if (!cJSON_IsNumber(frequency) || !cJSON_IsNumber(duration) ||
        !std::isfinite(frequency->valuedouble) ||
        !std::isfinite(duration->valuedouble) ||
        duration->valuedouble != std::floor(duration->valuedouble)) {
      return false;
    }
    const Limits limits{};
    return frequency->valuedouble >= limits.min_flash_hz &&
           frequency->valuedouble <= limits.max_flash_hz &&
           duration->valuedouble >= 1.0 &&
           duration->valuedouble <=
               static_cast<double>(limits.max_flash_duration_ms);
  }

  if (command_type == "LASER_STATE_RESYNC") {
    if (!payload_has_only(payload, "state")) return false;
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(payload, "state");
    return cJSON_IsString(state) && state->valuestring != nullptr &&
           (std::strcmp(state->valuestring, "OFF") == 0 ||
            std::strcmp(state->valuestring, "ON") == 0);
  }

  return false;
}

std::string print_json(cJSON *root) {
  if (root == nullptr) return {};
  char *text = cJSON_PrintUnformatted(root);
  std::string out = text != nullptr ? text : "";
  if (text != nullptr) cJSON_free(text);
  return out;
}

std::string reject(const std::string &device_id,
                   const std::string &command_id,
                   const char *code,
                   const char *category,
                   const char *message,
                   bool retryable) {
  return make_command_result(device_id, command_id, "REJECTED",
                             code, category, message, retryable);
}

}  // namespace

bool CommandJournal::Lookup(const std::string &command_id,
                            std::string *response_json) const {
  if (command_id.empty() || response_json == nullptr) return false;
  for (const Entry &entry : entries_) {
    if (entry.command_id == command_id && !entry.response_json.empty()) {
      *response_json = entry.response_json;
      return true;
    }
  }
  return false;
}

void CommandJournal::Remember(const std::string &command_id,
                              const std::string &response_json) {
  if (command_id.empty() || response_json.empty()) return;

  for (Entry &entry : entries_) {
    if (entry.command_id == command_id) {
      entry.response_json = response_json;
      return;
    }
  }
  entries_[next_].command_id = command_id;
  entries_[next_].response_json = response_json;
  next_ = (next_ + 1) % kCapacity;
}

std::string make_command_result(
    const std::string &device_id,
    const std::string &command_id,
    const char *status,
    const char *error_code,
    const char *category,
    const char *message,
    bool retryable,
    const std::string &payload_json) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return {};
  cJSON_AddStringToObject(root, "type", "command.result");
  cJSON_AddNumberToObject(root, "schema_version", 2);
  cJSON_AddStringToObject(root, "device_id", device_id.c_str());
  cJSON_AddStringToObject(root, "command_id", command_id.c_str());
  cJSON_AddStringToObject(root, "status",
                          status != nullptr ? status : "FAILED");

  if (!payload_json.empty()) {
    cJSON *payload =
        cJSON_ParseWithLength(payload_json.data(), payload_json.size());
    if (payload == nullptr || !cJSON_IsObject(payload)) {
      if (payload != nullptr) cJSON_Delete(payload);
      cJSON_Delete(root);
      return {};
    }
    cJSON_AddItemToObject(root, "payload", payload);
  }

  if (error_code != nullptr && error_code[0] != '\0') {
    cJSON *error = cJSON_CreateObject();
    if (error == nullptr) {
      cJSON_Delete(root);
      return {};
    }
    cJSON_AddStringToObject(error, "error_code", error_code);
    cJSON_AddStringToObject(error, "category",
                            category != nullptr ? category : "RUNTIME");
    cJSON_AddStringToObject(error, "message",
                            message != nullptr ? message : "");
    cJSON_AddBoolToObject(error, "retryable", retryable);
    cJSON_AddStringToObject(error, "affected_entity_id",
                            device_id.c_str());
    cJSON_AddItemToObject(root, "error", error);
  }

  const std::string out = print_json(root);
  cJSON_Delete(root);
  return out;
}

esp_err_t evaluate_command_execute_frame(
    const std::string &frame_json,
    const std::string &expected_device_id,
    const std::string &expected_project_id,
    const std::string &expected_runtime_snapshot_id,
    CommandJournal *journal,
    CommandDecision *decision) {
  if (frame_json.empty() || frame_json.size() > 8192 ||
      expected_device_id.empty() || expected_project_id.empty() ||
      expected_runtime_snapshot_id.empty() ||
      journal == nullptr || decision == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  *decision = CommandDecision{};

  cJSON *root =
      cJSON_ParseWithLength(frame_json.data(), frame_json.size());
  if (root == nullptr || !cJSON_IsObject(root)) {
    if (root != nullptr) cJSON_Delete(root);
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (!no_unknown_fields(root, allowed_root_key)) {
    cJSON_Delete(root);
    return ESP_ERR_INVALID_RESPONSE;
  }

  std::string type;
  std::string device_id;
  int outer_schema = 0;
  const bool outer_ok =
      string_field(root, "type", &type, true, 64) &&
      number_field(root, "schema_version", &outer_schema) &&
      string_field(root, "device_id", &device_id, true, kMaxID) &&
      type == "command.execute" &&
      outer_schema == kOuterSchemaVersion &&
      device_id == expected_device_id;

  const cJSON *command =
      cJSON_GetObjectItemCaseSensitive(root, "command");
  if (!outer_ok || !cJSON_IsObject(command) ||
      !no_unknown_fields(command, allowed_command_key)) {
    cJSON_Delete(root);
    return ESP_ERR_INVALID_RESPONSE;
  }

  CommandEnvelope parsed;
  int command_schema = 0;
  const bool fields_ok =
      string_field(command, "command_id", &parsed.command_id,
                   true, kMaxID) &&
      string_field(command, "command_type", &parsed.command_type,
                   true, kMaxCommandType) &&
      number_field(command, "schema_version", &command_schema) &&
      string_field(command, "issued_at", &parsed.issued_at, true, 64) &&
      string_field(command, "deadline_at", &parsed.deadline_at, false, 64) &&
      string_field(command, "project_id", &parsed.project_id, true, kMaxID) &&
      string_field(command, "runtime_snapshot_id",
                   &parsed.runtime_snapshot_id, true, kMaxID) &&
      positive_control_generation(command, &parsed.control_generation) &&
      string_field(command, "issuer", &parsed.issuer, true, kMaxIssuer) &&
      string_field(command, "correlation_id",
                   &parsed.correlation_id, false, kMaxID) &&
      string_field(command, "causation_id",
                   &parsed.causation_id, false, kMaxID) &&
      string_field(command, "priority",
                   &parsed.priority, true, kMaxPriority) &&
      string_field(command, "idempotency_key",
                   &parsed.idempotency_key, false, kMaxIdempotency);

  parsed.schema_version = command_schema;
  parsed.has_deadline = !parsed.deadline_at.empty();

  const cJSON *payload =
      cJSON_GetObjectItemCaseSensitive(command, "payload");

  if (!fields_ok || !cJSON_IsObject(payload)) {
    parsed.command_id =
        parsed.command_id.empty() ? "unknown" : parsed.command_id;
    decision->command = parsed;
    decision->disposition = CommandDisposition::kRejected;
    decision->response_json =
        reject(expected_device_id, parsed.command_id,
               "DEVICE_COMMAND_INVALID", "VALIDATION",
               "Command envelope is malformed", false);
    if (parsed.command_id != "unknown") {
      journal->Remember(parsed.command_id, decision->response_json);
    }
    cJSON_Delete(root);
    return ESP_OK;
  }

  std::string cached;
  if (journal->Lookup(parsed.command_id, &cached)) {
    decision->command = parsed;
    decision->disposition = CommandDisposition::kDuplicate;
    decision->response_json = cached;
    cJSON_Delete(root);
    return ESP_OK;
  }

  if (parsed.schema_version != kCommandSchemaVersion ||
      parsed.project_id != expected_project_id ||
      parsed.runtime_snapshot_id != expected_runtime_snapshot_id ||
      !supported_command_type(parsed.command_type) ||
      !valid_payload(parsed.command_type, payload)) {
    decision->command = parsed;
    decision->disposition = CommandDisposition::kRejected;
    decision->response_json =
        reject(expected_device_id, parsed.command_id,
               "DEVICE_COMMAND_INVALID", "VALIDATION",
               "Command schema, scope, type, or payload is not accepted",
               false);
    journal->Remember(parsed.command_id, decision->response_json);
    cJSON_Delete(root);
    return ESP_OK;
  }

  char *payload_text = cJSON_PrintUnformatted(payload);
  if (payload_text == nullptr) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }
  parsed.payload_json = payload_text;
  cJSON_free(payload_text);
  if (parsed.payload_json.size() > kMaxPayloadBytes) {
    decision->command = parsed;
    decision->disposition = CommandDisposition::kRejected;
    decision->response_json =
        reject(expected_device_id, parsed.command_id,
               "DEVICE_COMMAND_INVALID", "VALIDATION",
               "Command payload exceeds device bounds", false);
    journal->Remember(parsed.command_id, decision->response_json);
    cJSON_Delete(root);
    return ESP_OK;
  }

  if (!parse_rfc3339_unix_ms(parsed.issued_at,
                             &parsed.issued_at_unix_ms) ||
      (parsed.has_deadline &&
       !parse_rfc3339_unix_ms(parsed.deadline_at,
                              &parsed.deadline_at_unix_ms)) ||
      (parsed.has_deadline &&
       parsed.deadline_at_unix_ms < parsed.issued_at_unix_ms)) {
    decision->command = parsed;
    decision->disposition = CommandDisposition::kRejected;
    decision->response_json =
        reject(expected_device_id, parsed.command_id,
               "DEVICE_COMMAND_INVALID", "TIMING",
               "Command timestamps are invalid", false);
    journal->Remember(parsed.command_id, decision->response_json);
    cJSON_Delete(root);
    return ESP_OK;
  }

  const bool enabling_command =
      requires_fresh_deadline(parsed.command_type, payload);
  if (enabling_command && !parsed.has_deadline) {
    decision->command = parsed;
    decision->disposition = CommandDisposition::kRejected;
    decision->response_json =
        reject(expected_device_id, parsed.command_id,
               "DEVICE_COMMAND_DEADLINE_REQUIRED", "TIMING",
               "Output-enabling StageLaser commands require a Hub deadline",
               false);
    journal->Remember(parsed.command_id, decision->response_json);
    cJSON_Delete(root);
    return ESP_OK;
  }

  if (parsed.has_deadline) {
    if (!trusted_clock_ready()) {
      decision->command = parsed;
      decision->disposition = CommandDisposition::kRejected;
      decision->response_json =
          reject(expected_device_id, parsed.command_id,
                 "DEVICE_CLOCK_UNTRUSTED", "TIMING",
                 "Device has no Hub-trusted UTC clock", true);
      journal->Remember(parsed.command_id, decision->response_json);
      cJSON_Delete(root);
      return ESP_OK;
    }

    const int64_t now_ms = trusted_now_unix_ms();
    // A far-future issued_at can bypass the persistent emergency OFF
    // timestamp watermark even though the deadline is still in the future.
    // Reject such future-dated enabling frames. Deliberate OFF commands
    // remain available during an inconsistent timestamp situation.
    if (enabling_command &&
        IssuedTooFarInFuture(parsed.issued_at_unix_ms, now_ms)) {
      decision->command = parsed;
      decision->disposition = CommandDisposition::kRejected;
      decision->response_json =
          reject(expected_device_id, parsed.command_id,
                 "DEVICE_COMMAND_ISSUED_IN_FUTURE", "TIMING",
                 "Output-enabling command issued_at exceeds clock tolerance",
                 false);
      journal->Remember(parsed.command_id, decision->response_json);
      cJSON_Delete(root);
      return ESP_OK;
    }
    if (now_ms <= 0 || now_ms > parsed.deadline_at_unix_ms) {
      decision->command = parsed;
      decision->disposition = CommandDisposition::kTimedOut;
      decision->response_json =
          make_command_result(expected_device_id, parsed.command_id,
                              "TIMED_OUT",
                              "DEVICE_COMMAND_EXPIRED", "TIMING",
                              "Command deadline has expired", false);
      journal->Remember(parsed.command_id, decision->response_json);
      cJSON_Delete(root);
      return ESP_OK;
    }
  }

  decision->command = std::move(parsed);
  decision->disposition = CommandDisposition::kReady;
  decision->response_json.clear();
  cJSON_Delete(root);
  return ESP_OK;
}

}  // namespace stagecore::stagelaser
