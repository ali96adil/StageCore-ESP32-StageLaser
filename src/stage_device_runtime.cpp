#include "stage_device_runtime.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "command_replay_store.h"
#include "laser_command_contract.h"
#include "laser_observation.h"

#ifndef STAGECORE_FW_VERSION
#define STAGECORE_FW_VERSION "0.1.0-dev"
#endif

namespace stagecore {
namespace {

constexpr char kTag[] = "stagelaser-runtime";
constexpr char kProtocolVersion[] = "stagecore.device/2";
constexpr size_t kMaxInboundBytes = 16384;
constexpr int kReadyTimeoutMs = 5000;
constexpr int kSafeOffTimeoutMs = 5000;
constexpr int kObservationPeriodMs = 5000;

constexpr EventBits_t kConnectedBit = BIT0;
constexpr EventBits_t kAssignmentBit = BIT1;
constexpr EventBits_t kPrepareBit = BIT2;
constexpr EventBits_t kDisconnectedBit = BIT3;
constexpr EventBits_t kProtocolErrorBit = BIT4;
constexpr EventBits_t kRuntimeReadyBit = BIT5;
constexpr EventBits_t kCommandBit = BIT6;

struct PrepareRequest {
  bool present = false;
  std::string assignment_id;
  int64_t assignment_epoch = 0;
  int64_t connection_generation = 0;
  std::string challenge;
  std::string target_project_id;
  std::string target_runtime_snapshot_id;
};

enum class PendingGoal {
  kNone,
  kOn,
  kOff,
  kSafeOff,
  kFlashStarted,
};

struct RuntimeCommandState {
  bool pending = false;
  bool controller_faulted = false;
  PendingGoal goal = PendingGoal::kNone;
  std::string command_id;
  std::string command_type;
  std::string response_command_id;
  stagelaser::FlashObservationInfo flash;
  std::string last_accepted_command_id;
  std::string last_applied_command_id;
  std::string last_command_type;
  std::string last_command_result;
};

struct RuntimeContext {
  EventGroupHandle_t events = nullptr;
  SemaphoreHandle_t lock = nullptr;
  std::string device_id;
  std::string inbound;
  int expected_payload = 0;

  bool assignment_received = false;
  std::string assignment_state;
  int64_t assignment_epoch = 0;
  int64_t connection_generation = 0;
  std::string project_id;
  std::string runtime_snapshot_id;

  PrepareRequest pending_prepare;
  bool commands_enabled = false;
  bool runtime_ready_received = false;
  std::vector<std::string> pending_command_frames;
  stagelaser::CommandJournal journal;
};

bool exact_positive_integer(const cJSON *root, const char *key, int64_t *out) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsNumber(value) || out == nullptr ||
      !std::isfinite(value->valuedouble) ||
      value->valuedouble < 1.0 ||
      value->valuedouble > 9007199254740991.0 ||
      std::floor(value->valuedouble) != value->valuedouble) {
    return false;
  }
  *out = static_cast<int64_t>(value->valuedouble);
  return true;
}

bool nonempty_string(const cJSON *root, const char *key, std::string *out) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!cJSON_IsString(item) || item->valuestring == nullptr ||
      item->valuestring[0] == '\0' || out == nullptr) {
    return false;
  }
  *out = item->valuestring;
  return true;
}

bool absent_or_empty_string(const cJSON *root, const char *key) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
  return item == nullptr ||
         (cJSON_IsString(item) && item->valuestring != nullptr &&
          item->valuestring[0] == '\0');
}

bool canonical_hex_64(const std::string &value) {
  if (value.size() != 64) return false;
  for (char ch : value) {
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
  }
  return true;
}

bool uuid_length(const std::string &value) {
  return value.size() == 36 &&
         value.find_first_of(" \t\r\n") == std::string::npos;
}

const char *reset_reason_text() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    default: return "UNKNOWN";
  }
}

stagelaser::ObservationMetadata make_observation_metadata(
    const RuntimeCommandState *commands = nullptr) {
  stagelaser::ObservationMetadata metadata;
  metadata.firmware_version = STAGECORE_FW_VERSION;
  metadata.uptime_seconds = esp_timer_get_time() / 1000000LL;
  metadata.reset_reason = reset_reason_text();

  wifi_ap_record_t ap{};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    metadata.has_wifi_rssi = true;
    metadata.wifi_rssi_dbm = ap.rssi;
  }
  if (commands != nullptr) {
    metadata.flash = commands->flash;
    metadata.last_accepted_command_id = commands->last_accepted_command_id;
    metadata.last_applied_command_id = commands->last_applied_command_id;
    metadata.last_command_type = commands->last_command_type;
    metadata.last_command_result = commands->last_command_result;
  }
  return metadata;
}

cJSON *network_state_json(const VerifiedHub &hub) {
  cJSON *network = cJSON_CreateObject();
  if (network == nullptr) return nullptr;
  cJSON_AddStringToObject(network, "transport", "TLS_WEBSOCKET");
  cJSON_AddStringToObject(network, "protocol", kProtocolVersion);
  cJSON_AddStringToObject(network, "hub_id", hub.hub_id.c_str());
  cJSON_AddBoolToObject(network, "certificate_pinned", true);
  return network;
}

std::string print_json(cJSON *root) {
  if (root == nullptr) return {};
  char *text = cJSON_PrintUnformatted(root);
  std::string out = text != nullptr ? text : "";
  if (text != nullptr) cJSON_free(text);
  return out;
}

cJSON *capabilities_json() {
  static constexpr const char *kCaps[] = {
      "laser.arm",
      "laser.disarm",
      "laser.state.set",
      "laser.flash.start",
      "laser.flash.stop",
      "laser.safe_off",
      "laser.state.read",
      "laser.state.resync",
  };
  cJSON *caps = cJSON_CreateArray();
  if (caps == nullptr) return nullptr;
  for (const char *capability : kCaps) {
    cJSON_AddItemToArray(caps, cJSON_CreateString(capability));
  }
  return caps;
}

std::string make_hello(const VerifiedHub &hub,
                       const DeviceIdentity &identity,
                       const DeviceConfig &config,
                       const stagelaser::LaserController &laser) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return {};

  cJSON_AddStringToObject(root, "type", "device.hello");
  cJSON_AddNumberToObject(root, "schema_version", 1);
  cJSON_AddStringToObject(root, "device_id", identity.device_id().c_str());
  cJSON_AddStringToObject(root, "profile_id", "stagecore.esp32-stagelaser");
  cJSON_AddStringToObject(root, "device_kind", "GENERIC");
  cJSON_AddStringToObject(root, "display_name", config.display_name.c_str());
  cJSON_AddStringToObject(root, "platform", "esp32");
  cJSON_AddStringToObject(root, "architecture", "riscv32");
  cJSON_AddStringToObject(root, "client_version", STAGECORE_FW_VERSION);
  cJSON_AddStringToObject(root, "protocol_version", kProtocolVersion);
  cJSON_AddStringToObject(root, "readiness", "BLOCKER");

  cJSON *caps = capabilities_json();
  cJSON *observed = stagelaser::make_observed_state_json(
      laser.machine(), make_observation_metadata());
  cJSON *network = network_state_json(hub);
  if (caps == nullptr || observed == nullptr || network == nullptr) {
    if (caps != nullptr) cJSON_Delete(caps);
    if (observed != nullptr) cJSON_Delete(observed);
    if (network != nullptr) cJSON_Delete(network);
    cJSON_Delete(root);
    return {};
  }
  cJSON_AddItemToObject(root, "capabilities", caps);
  cJSON_AddItemToObject(root, "observed_state", observed);
  cJSON_AddItemToObject(root, "network_state", network);

  const std::string out = print_json(root);
  cJSON_Delete(root);
  return out;
}

std::string make_observation(const VerifiedHub &hub,
                             const std::string &device_id,
                             const stagelaser::LaserController &laser,
                             const RuntimeCommandState *commands,
                             bool ready) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return {};
  cJSON_AddStringToObject(root, "type", "device.observation");
  cJSON_AddNumberToObject(root, "schema_version", 2);
  cJSON_AddStringToObject(root, "device_id", device_id.c_str());
  cJSON_AddStringToObject(root, "readiness", ready ? "READY" : "BLOCKER");

  cJSON *observed = stagelaser::make_observed_state_json(
      laser.machine(), make_observation_metadata(commands));
  cJSON *network = network_state_json(hub);
  if (observed == nullptr || network == nullptr) {
    if (observed != nullptr) cJSON_Delete(observed);
    if (network != nullptr) cJSON_Delete(network);
    cJSON_Delete(root);
    return {};
  }
  cJSON_AddItemToObject(root, "observed_state", observed);
  cJSON_AddItemToObject(root, "network_state", network);
  const std::string out = print_json(root);
  cJSON_Delete(root);
  return out;
}

bool parse_assignment_state(RuntimeContext *context, const cJSON *root) {
  if (context == nullptr || context->assignment_received) return false;

  std::string state;
  const cJSON *state_item = cJSON_GetObjectItemCaseSensitive(root, "state");
  const cJSON *commands = cJSON_GetObjectItemCaseSensitive(root, "commands_enabled");
  if (!cJSON_IsString(state_item) || state_item->valuestring == nullptr ||
      !cJSON_IsFalse(commands)) {
    return false;
  }
  state = state_item->valuestring;

  int64_t epoch = 0;
  int64_t generation = 0;
  if (!exact_positive_integer(root, "assignment_epoch", &epoch) ||
      !exact_positive_integer(root, "connection_generation", &generation)) {
    return false;
  }

  const cJSON *safe_off =
      cJSON_GetObjectItemCaseSensitive(root, "safe_off_required");
  if (!cJSON_IsTrue(safe_off)) return false;

  std::string project;
  std::string snapshot;
  if (state == "UNASSIGNED") {
    if (!absent_or_empty_string(root, "project_id") ||
        !absent_or_empty_string(root, "runtime_snapshot_id")) {
      return false;
    }
  } else if (state == "ACTIVE") {
    const cJSON *scope =
        cJSON_GetObjectItemCaseSensitive(root, "scope_ack_required");
    if (!cJSON_IsTrue(scope) ||
        !nonempty_string(root, "project_id", &project) ||
        !nonempty_string(root, "runtime_snapshot_id", &snapshot)) {
      return false;
    }
  } else if (state == "BLOCKED") {
    if (!nonempty_string(root, "project_id", &project) ||
        !absent_or_empty_string(root, "runtime_snapshot_id")) {
      return false;
    }
  } else {
    return false;
  }

  context->assignment_received = true;
  context->assignment_state = state;
  context->assignment_epoch = epoch;
  context->connection_generation = generation;
  context->project_id = project;
  context->runtime_snapshot_id = snapshot;
  xEventGroupSetBits(context->events, kAssignmentBit);
  return true;
}

bool parse_prepare(RuntimeContext *context, const cJSON *root) {
  if (context == nullptr || !context->assignment_received ||
      context->lock == nullptr) {
    return false;
  }

  PrepareRequest request;
  if (!nonempty_string(root, "assignment_id", &request.assignment_id) ||
      !uuid_length(request.assignment_id) ||
      !exact_positive_integer(root, "assignment_epoch",
                              &request.assignment_epoch) ||
      !exact_positive_integer(root, "connection_generation",
                              &request.connection_generation) ||
      !nonempty_string(root, "challenge", &request.challenge) ||
      !canonical_hex_64(request.challenge) ||
      !nonempty_string(root, "target_project_id",
                       &request.target_project_id) ||
      !nonempty_string(root, "target_runtime_snapshot_id",
                       &request.target_runtime_snapshot_id)) {
    return false;
  }

  const cJSON *safe_off =
      cJSON_GetObjectItemCaseSensitive(root, "safe_off_required");
  const cJSON *arm =
      cJSON_GetObjectItemCaseSensitive(root, "required_arm_state");
  const cJSON *logical =
      cJSON_GetObjectItemCaseSensitive(root, "required_logical_state");
  if (!cJSON_IsTrue(safe_off) ||
      !cJSON_IsString(arm) || arm->valuestring == nullptr ||
      std::strcmp(arm->valuestring, "DISARMED") != 0 ||
      !cJSON_IsString(logical) || logical->valuestring == nullptr ||
      std::strcmp(logical->valuestring, "OFF") != 0 ||
      request.assignment_epoch != context->assignment_epoch ||
      request.connection_generation != context->connection_generation) {
    return false;
  }

  if (xSemaphoreTake(context->lock, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;
  }
  const bool free = !context->pending_prepare.present;
  if (free) {
    request.present = true;
    context->pending_prepare = request;
  }
  xSemaphoreGive(context->lock);
  if (!free) return false;

  xEventGroupSetBits(context->events, kPrepareBit);
  return true;
}

bool parse_runtime_ready(RuntimeContext *context, const cJSON *root) {
  if (context == nullptr || !context->assignment_received ||
      context->assignment_state != "ACTIVE" ||
      context->runtime_ready_received) {
    return false;
  }

  std::string protocol;
  std::string project;
  std::string snapshot;
  int64_t epoch = 0;
  int64_t generation = 0;
  const cJSON *commands =
      cJSON_GetObjectItemCaseSensitive(root, "commands_enabled");
  if (!nonempty_string(root, "protocol_version", &protocol) ||
      protocol != kProtocolVersion ||
      !nonempty_string(root, "project_id", &project) ||
      !nonempty_string(root, "runtime_snapshot_id", &snapshot) ||
      !exact_positive_integer(root, "assignment_epoch", &epoch) ||
      !exact_positive_integer(root, "connection_generation", &generation) ||
      !cJSON_IsTrue(commands) ||
      project != context->project_id ||
      snapshot != context->runtime_snapshot_id ||
      epoch != context->assignment_epoch ||
      generation != context->connection_generation) {
    return false;
  }

  context->runtime_ready_received = true;
  context->commands_enabled = true;
  xEventGroupSetBits(context->events, kRuntimeReadyBit);
  return true;
}

bool queue_command(RuntimeContext *context, const std::string &text) {
  if (context == nullptr || !context->commands_enabled ||
      context->lock == nullptr || text.empty()) {
    return false;
  }
  if (xSemaphoreTake(context->lock, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;
  }
  bool accepted = false;
  if (context->pending_command_frames.size() < 8) {
    context->pending_command_frames.push_back(text);
    accepted = true;
  }
  xSemaphoreGive(context->lock);
  if (accepted) xEventGroupSetBits(context->events, kCommandBit);
  return accepted;
}

bool handle_complete_text(RuntimeContext *context, const std::string &text) {
  cJSON *root = cJSON_ParseWithLength(text.data(), text.size());
  if (root == nullptr) return false;

  const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
  const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
  const cJSON *device = cJSON_GetObjectItemCaseSensitive(root, "device_id");
  bool ok = cJSON_IsString(type) && type->valuestring != nullptr &&
            cJSON_IsNumber(schema) && schema->valuedouble == 2.0 &&
            cJSON_IsString(device) && device->valuestring != nullptr &&
            context != nullptr && context->device_id == device->valuestring;

  if (ok && std::strcmp(type->valuestring, "assignment.state") == 0) {
    ok = parse_assignment_state(context, root);
  } else if (ok &&
             std::strcmp(type->valuestring,
                         "stagelaser.assignment.prepare") == 0) {
    ok = parse_prepare(context, root);
  } else if (ok && std::strcmp(type->valuestring, "runtime.ready") == 0) {
    ok = parse_runtime_ready(context, root);
  } else if (ok && std::strcmp(type->valuestring, "command.execute") == 0) {
    ok = queue_command(context, text);
  } else {
    ok = false;
  }

  cJSON_Delete(root);
  return ok;
}

void runtime_event_handler(void *arg, esp_event_base_t,
                           int32_t event_id, void *event_data) {
  auto *context = static_cast<RuntimeContext *>(arg);
  if (context == nullptr || context->events == nullptr) return;

  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      xEventGroupSetBits(context->events, kConnectedBit);
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
      xEventGroupSetBits(context->events, kDisconnectedBit);
      break;
    case WEBSOCKET_EVENT_DATA: {
      auto *data = static_cast<esp_websocket_event_data_t *>(event_data);
      if (data == nullptr) {
        xEventGroupSetBits(context->events, kProtocolErrorBit);
        break;
      }
      if (data->op_code != 0x1 && data->op_code != 0x0) break;
      if (data->payload_len <= 0 ||
          static_cast<size_t>(data->payload_len) > kMaxInboundBytes ||
          data->payload_offset < 0 || data->data_len < 0) {
        xEventGroupSetBits(context->events, kProtocolErrorBit);
        break;
      }
      if (data->payload_offset == 0) {
        context->inbound.clear();
        context->expected_payload = data->payload_len;
        context->inbound.reserve(static_cast<size_t>(data->payload_len));
      }
      if (context->expected_payload != data->payload_len ||
          static_cast<int>(context->inbound.size()) != data->payload_offset ||
          context->inbound.size() + static_cast<size_t>(data->data_len) >
              kMaxInboundBytes) {
        xEventGroupSetBits(context->events, kProtocolErrorBit);
        break;
      }
      if (data->data_ptr != nullptr && data->data_len > 0) {
        context->inbound.append(data->data_ptr,
                                static_cast<size_t>(data->data_len));
      }
      const bool complete =
          data->fin &&
          static_cast<int>(context->inbound.size()) == context->expected_payload;
      if (complete && !handle_complete_text(context, context->inbound)) {
        xEventGroupSetBits(context->events, kProtocolErrorBit);
      }
      break;
    }
    default:
      break;
  }
}

esp_err_t send_text(esp_websocket_client_handle_t client,
                    const std::string &message) {
  if (client == nullptr || message.empty()) return ESP_ERR_INVALID_ARG;
  const int sent = esp_websocket_client_send_text(
      client, message.data(), static_cast<int>(message.size()),
      pdMS_TO_TICKS(3000));
  return sent == static_cast<int>(message.size()) ? ESP_OK : ESP_FAIL;
}

bool take_prepare(RuntimeContext *context, PrepareRequest *request) {
  if (context == nullptr || request == nullptr || context->lock == nullptr) {
    return false;
  }
  if (xSemaphoreTake(context->lock, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;
  }
  const bool present = context->pending_prepare.present;
  if (present) {
    *request = context->pending_prepare;
    context->pending_prepare = PrepareRequest{};
  }
  xSemaphoreGive(context->lock);
  if (present) xEventGroupClearBits(context->events, kPrepareBit);
  return present;
}

bool take_command(RuntimeContext *context, std::string *frame) {
  if (context == nullptr || frame == nullptr || context->lock == nullptr) {
    return false;
  }
  if (xSemaphoreTake(context->lock, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;
  }
  const bool present = !context->pending_command_frames.empty();
  if (present) {
    *frame = std::move(context->pending_command_frames.front());
    context->pending_command_frames.erase(context->pending_command_frames.begin());
  }
  const bool more = !context->pending_command_frames.empty();
  xSemaphoreGive(context->lock);
  if (!more) xEventGroupClearBits(context->events, kCommandBit);
  return present;
}

bool known_safe_off(const stagelaser::LaserController &laser) {
  const auto &m = laser.machine();
  return m.arm_state() == stagelaser::ArmState::kDisarmed &&
         m.logical_state() == stagelaser::LogicalState::kOff &&
         (m.state_quality() == stagelaser::StateQuality::kTracked ||
          m.state_quality() == stagelaser::StateQuality::kConfirmed) &&
         !m.resync_required() && !m.pulse_in_progress() &&
         !m.flash_active();
}

esp_err_t drive_safe_off(stagelaser::LaserController *laser) {
  if (laser == nullptr) return ESP_ERR_INVALID_ARG;
  auto outcome = laser->SafeOff(esp_timer_get_time() / 1000ULL);
  if (outcome.fault != stagelaser::ControllerFault::kNone) {
    return ESP_ERR_INVALID_STATE;
  }

  const int64_t deadline_us =
      esp_timer_get_time() + static_cast<int64_t>(kSafeOffTimeoutMs) * 1000LL;
  while (!known_safe_off(*laser)) {
    if (laser->machine().resync_required() ||
        esp_timer_get_time() >= deadline_us) {
      return ESP_ERR_INVALID_STATE;
    }
    outcome = laser->Poll(esp_timer_get_time() / 1000ULL);
    if (outcome.fault != stagelaser::ControllerFault::kNone) {
      return ESP_ERR_INVALID_STATE;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return ESP_OK;
}

std::string make_safe_ack(const VerifiedHub &hub,
                          const std::string &device_id,
                          const PrepareRequest &request,
                          const stagelaser::LaserController &laser) {
  if (!known_safe_off(laser)) return {};

  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return {};
  cJSON_AddStringToObject(root, "type", "stagelaser.assignment.safe_ack");
  cJSON_AddNumberToObject(root, "schema_version", 2);
  cJSON_AddStringToObject(root, "device_id", device_id.c_str());
  cJSON_AddStringToObject(root, "assignment_id",
                          request.assignment_id.c_str());
  cJSON_AddNumberToObject(root, "assignment_epoch",
                          static_cast<double>(request.assignment_epoch));
  cJSON_AddNumberToObject(root, "connection_generation",
                          static_cast<double>(request.connection_generation));
  cJSON_AddStringToObject(root, "challenge", request.challenge.c_str());
  cJSON_AddStringToObject(root, "readiness", "BLOCKER");

  cJSON *observed = stagelaser::make_observed_state_json(
      laser.machine(), make_observation_metadata());
  cJSON *network = network_state_json(hub);
  if (observed == nullptr || network == nullptr) {
    if (observed != nullptr) cJSON_Delete(observed);
    if (network != nullptr) cJSON_Delete(network);
    cJSON_Delete(root);
    return {};
  }
  cJSON_AddItemToObject(root, "observed_state", observed);
  cJSON_AddItemToObject(root, "network_state", network);
  const std::string out = print_json(root);
  cJSON_Delete(root);
  return out;
}

std::string make_scope_ack(const VerifiedHub &hub,
                           const RuntimeContext &context,
                           const stagelaser::LaserController &laser) {
  if (context.assignment_state != "ACTIVE" ||
      context.project_id.empty() || context.runtime_snapshot_id.empty() ||
      !known_safe_off(laser)) {
    return {};
  }
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return {};
  cJSON_AddStringToObject(root, "type", "stagelaser.assignment.scope_ack");
  cJSON_AddNumberToObject(root, "schema_version", 2);
  cJSON_AddStringToObject(root, "device_id", context.device_id.c_str());
  cJSON_AddStringToObject(root, "project_id", context.project_id.c_str());
  cJSON_AddStringToObject(root, "runtime_snapshot_id",
                          context.runtime_snapshot_id.c_str());
  cJSON_AddNumberToObject(root, "assignment_epoch",
                          static_cast<double>(context.assignment_epoch));
  cJSON_AddNumberToObject(root, "connection_generation",
                          static_cast<double>(context.connection_generation));
  cJSON_AddStringToObject(root, "readiness", "READY");

  cJSON *observed = stagelaser::make_observed_state_json(
      laser.machine(), make_observation_metadata());
  cJSON *network = network_state_json(hub);
  if (observed == nullptr || network == nullptr) {
    if (observed != nullptr) cJSON_Delete(observed);
    if (network != nullptr) cJSON_Delete(network);
    cJSON_Delete(root);
    return {};
  }
  cJSON_AddItemToObject(root, "observed_state", observed);
  cJSON_AddItemToObject(root, "network_state", network);
  const std::string result = print_json(root);
  cJSON_Delete(root);
  return result;
}

const char *arm_text(stagelaser::ArmState state) {
  return state == stagelaser::ArmState::kArmed ? "ARMED" : "DISARMED";
}

const char *logical_text(stagelaser::LogicalState state) {
  using stagelaser::LogicalState;
  switch (state) {
    case LogicalState::kOff: return "OFF";
    case LogicalState::kTurningOn: return "TURNING_ON";
    case LogicalState::kOn: return "ON";
    case LogicalState::kTurningOff: return "TURNING_OFF";
    case LogicalState::kFlashOn: return "FLASH_ON";
    case LogicalState::kFlashOff: return "FLASH_OFF";
    case LogicalState::kError: return "ERROR";
    case LogicalState::kUnknown:
    default: return "UNKNOWN";
  }
}

const char *quality_text(stagelaser::StateQuality state) {
  using stagelaser::StateQuality;
  switch (state) {
    case StateQuality::kTracked: return "TRACKED";
    case StateQuality::kConfirmed: return "CONFIRMED";
    case StateQuality::kUnknown:
    default: return "UNKNOWN";
  }
}

std::string state_result_payload(const stagelaser::LaserController &laser) {
  const auto &m = laser.machine();
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return {};
  cJSON_AddStringToObject(root, "arm_state", arm_text(m.arm_state()));
  cJSON_AddStringToObject(root, "logical_state", logical_text(m.logical_state()));
  cJSON_AddStringToObject(root, "state_quality", quality_text(m.state_quality()));
  cJSON_AddBoolToObject(root, "resync_required", m.resync_required());
  cJSON_AddBoolToObject(root, "pulse_in_progress", m.pulse_in_progress());
  cJSON_AddNumberToObject(root, "relay_pulse_count",
                          static_cast<double>(m.relay_pulse_count()));
  const std::string result = print_json(root);
  cJSON_Delete(root);
  return result;
}

std::string rejection_for_decision(
    const std::string &device_id,
    const std::string &command_id,
    stagelaser::ResultCode result) {
  using stagelaser::ResultCode;
  switch (result) {
    case ResultCode::kRejectedDisarmed:
      return stagelaser::make_command_result(
          device_id, command_id, "REJECTED", "LASER_NOT_ARMED", "SAFETY",
          "StageLaser must be armed before this command", false);
    case ResultCode::kRejectedUnknown:
      return stagelaser::make_command_result(
          device_id, command_id, "REJECTED", "LASER_STATE_UNKNOWN", "SAFETY",
          "StageLaser logical state is unknown; resync is required", false);
    case ResultCode::kRejectedBusy:
      return stagelaser::make_command_result(
          device_id, command_id, "REJECTED", "LASER_BUSY", "RUNTIME",
          "StageLaser is busy with another transition", true);
    case ResultCode::kRejectedLimits:
      return stagelaser::make_command_result(
          device_id, command_id, "REJECTED", "LASER_COMMAND_INVALID",
          "VALIDATION", "StageLaser command exceeds configured limits", false);
    case ResultCode::kRejectedUnsafe:
      return stagelaser::make_command_result(
          device_id, command_id, "REJECTED", "LASER_STATE_UNSAFE", "SAFETY",
          "StageLaser cannot prove a deterministic safe transition", false);
    default:
      return {};
  }
}

std::string failure_for_fault(
    const std::string &device_id,
    const std::string &command_id,
    stagelaser::ControllerFault fault) {
  const char *message = "StageLaser controller failed";
  const char *category = "RUNTIME";
  switch (fault) {
    case stagelaser::ControllerFault::kPersistence:
      message = "StageLaser could not persist physical truth before actuation";
      category = "PERSISTENCE";
      break;
    case stagelaser::ControllerFault::kActuatorPick:
      message = "StageLaser relay contact could not be asserted";
      category = "HARDWARE";
      break;
    case stagelaser::ControllerFault::kActuatorRelease:
      message = "StageLaser relay contact release could not be confirmed";
      category = "HARDWARE";
      break;
    case stagelaser::ControllerFault::kNone:
      return {};
  }
  return stagelaser::make_command_result(
      device_id, command_id, "FAILED", "LASER_STATE_UNSAFE",
      category, message, false);
}

void mark_result(RuntimeCommandState *state,
                 const stagelaser::CommandEnvelope &command,
                 const char *status,
                 bool applied) {
  if (state == nullptr) return;
  state->last_command_type = command.command_type;
  state->last_command_result = status != nullptr ? status : "";
  if (status != nullptr &&
      (std::strcmp(status, "ACCEPTED") == 0 ||
       std::strcmp(status, "COMPLETED") == 0)) {
    state->last_accepted_command_id = command.command_id;
  }
  if (applied) state->last_applied_command_id = command.command_id;
}

bool parse_flash_payload(const std::string &payload_json,
                         stagelaser::FlashRequest *request) {
  if (request == nullptr) return false;
  cJSON *root = cJSON_ParseWithLength(payload_json.data(), payload_json.size());
  if (root == nullptr) return false;
  const cJSON *frequency = cJSON_GetObjectItemCaseSensitive(root, "frequency_hz");
  const cJSON *duration = cJSON_GetObjectItemCaseSensitive(root, "duration_ms");
  const bool ok = cJSON_IsNumber(frequency) && cJSON_IsNumber(duration) &&
                  std::isfinite(frequency->valuedouble) &&
                  std::isfinite(duration->valuedouble) &&
                  duration->valuedouble == std::floor(duration->valuedouble);
  if (ok) {
    request->frequency_hz = frequency->valuedouble;
    request->duration_ms = static_cast<uint32_t>(duration->valuedouble);
  }
  cJSON_Delete(root);
  return ok;
}

bool parse_resync_payload(const std::string &payload_json, bool *target_on) {
  if (target_on == nullptr) return false;
  cJSON *root = cJSON_ParseWithLength(payload_json.data(), payload_json.size());
  if (root == nullptr) return false;
  const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
  bool ok = cJSON_IsString(state) && state->valuestring != nullptr;
  if (ok && std::strcmp(state->valuestring, "ON") == 0) {
    *target_on = true;
  } else if (ok && std::strcmp(state->valuestring, "OFF") == 0) {
    *target_on = false;
  } else {
    ok = false;
  }
  cJSON_Delete(root);
  return ok;
}

bool known_on(const stagelaser::LaserController &laser) {
  const auto &m = laser.machine();
  return m.logical_state() == stagelaser::LogicalState::kOn &&
         !m.pulse_in_progress() && !m.resync_required();
}

bool known_off(const stagelaser::LaserController &laser) {
  const auto &m = laser.machine();
  return m.logical_state() == stagelaser::LogicalState::kOff &&
         !m.pulse_in_progress() && !m.flash_active() &&
         !m.resync_required();
}

bool command_may_actuate(const std::string &command_type) {
  return command_type == "LASER_DISARM" ||
         command_type == "LASER_SET_ON" ||
         command_type == "LASER_SET_OFF" ||
         command_type == "LASER_FLASH_START" ||
         command_type == "LASER_FLASH_STOP" ||
         command_type == "LASER_SAFE_OFF";
}

std::string start_ready_command(
    const stagelaser::CommandEnvelope &command,
    const std::string &device_id,
    stagelaser::LaserController *laser,
    RuntimeCommandState *state,
    uint64_t now_ms) {
  if (laser == nullptr || state == nullptr) return {};
  if (state->pending) {
    mark_result(state, command, "REJECTED", false);
    return stagelaser::make_command_result(
        device_id, command.command_id, "REJECTED", "LASER_BUSY", "RUNTIME",
        "StageLaser already has a command awaiting a stable output state", true);
  }

  if (command_may_actuate(command.command_type)) {
    bool seen = false;
    esp_err_t replay_err =
        stagelaser::command_replay_seen(command.command_id, &seen);
    if (replay_err != ESP_OK) {
      mark_result(state, command, "FAILED", false);
      return stagelaser::make_command_result(
          device_id, command.command_id, "FAILED",
          "DEVICE_PERSISTENCE_FAILED", "PERSISTENCE",
          "StageLaser replay fence could not be read", false);
    }
    if (seen) {
      mark_result(state, command, "REJECTED", false);
      return stagelaser::make_command_result(
          device_id, command.command_id, "REJECTED",
          "DEVICE_COMMAND_DUPLICATE", "IDEMPOTENCY",
          "This actuation command_id was already accepted before", false);
    }
    replay_err = stagelaser::command_replay_remember(command.command_id);
    if (replay_err != ESP_OK) {
      mark_result(state, command, "FAILED", false);
      return stagelaser::make_command_result(
          device_id, command.command_id, "FAILED",
          "DEVICE_PERSISTENCE_FAILED", "PERSISTENCE",
          "StageLaser replay fence could not be persisted before actuation",
          false);
    }
  }

  stagelaser::ControllerOutcome outcome{};
  bool has_outcome = true;
  PendingGoal goal = PendingGoal::kNone;

  if (command.command_type == "LASER_ARM") {
    outcome = laser->Arm(now_ms);
  } else if (command.command_type == "LASER_DISARM") {
    outcome = laser->Disarm(now_ms);
    goal = PendingGoal::kSafeOff;
  } else if (command.command_type == "LASER_SET_ON") {
    outcome = laser->SetOn(now_ms);
    goal = PendingGoal::kOn;
  } else if (command.command_type == "LASER_SET_OFF") {
    outcome = laser->SetOff(now_ms);
    goal = PendingGoal::kOff;
  } else if (command.command_type == "LASER_FLASH_START") {
    stagelaser::FlashRequest request;
    if (!parse_flash_payload(command.payload_json, &request)) {
      mark_result(state, command, "REJECTED", false);
      return stagelaser::make_command_result(
          device_id, command.command_id, "REJECTED",
          "LASER_COMMAND_INVALID", "VALIDATION",
          "Flash payload could not be decoded", false);
    }
    outcome = laser->FlashStart(now_ms, request);
    goal = PendingGoal::kFlashStarted;
    if (outcome.fault == stagelaser::ControllerFault::kNone &&
        (outcome.decision.result == stagelaser::ResultCode::kAccepted ||
         outcome.decision.result == stagelaser::ResultCode::kNoop)) {
      state->flash.active = true;
      state->flash.command_id = command.command_id;
      state->flash.frequency_hz = request.frequency_hz;
      state->flash.duration_ms = request.duration_ms;
    }
  } else if (command.command_type == "LASER_FLASH_STOP") {
    outcome = laser->FlashStop(now_ms);
    goal = PendingGoal::kOff;
  } else if (command.command_type == "LASER_SAFE_OFF") {
    outcome = laser->SafeOff(now_ms);
    goal = PendingGoal::kSafeOff;
  } else if (command.command_type == "LASER_STATE_RESYNC") {
    bool target_on = false;
    if (!parse_resync_payload(command.payload_json, &target_on)) {
      mark_result(state, command, "REJECTED", false);
      return stagelaser::make_command_result(
          device_id, command.command_id, "REJECTED",
          "LASER_COMMAND_INVALID", "VALIDATION",
          "Resync payload could not be decoded", false);
    }
    outcome = target_on ? laser->ResyncOn() : laser->ResyncOff();
  } else if (command.command_type == "LASER_STATE_READ") {
    has_outcome = false;
  } else {
    mark_result(state, command, "REJECTED", false);
    return stagelaser::make_command_result(
        device_id, command.command_id, "REJECTED",
        "LASER_COMMAND_INVALID", "VALIDATION",
        "Unsupported StageLaser command", false);
  }

  if (has_outcome && outcome.fault != stagelaser::ControllerFault::kNone) {
    state->flash.active = false;
    mark_result(state, command, "FAILED", false);
    return failure_for_fault(device_id, command.command_id, outcome.fault);
  }

  if (has_outcome &&
      outcome.decision.result != stagelaser::ResultCode::kAccepted &&
      outcome.decision.result != stagelaser::ResultCode::kNoop) {
    if (command.command_type == "LASER_FLASH_START") {
      state->flash = stagelaser::FlashObservationInfo{};
    }
    mark_result(state, command, "REJECTED", false);
    return rejection_for_decision(
        device_id, command.command_id, outcome.decision.result);
  }

  bool complete = false;
  if (command.command_type == "LASER_ARM") {
    complete = laser->machine().arm_state() == stagelaser::ArmState::kArmed;
  } else if (command.command_type == "LASER_DISARM" ||
             command.command_type == "LASER_SAFE_OFF") {
    complete = known_safe_off(*laser);
  } else if (command.command_type == "LASER_SET_ON") {
    complete = known_on(*laser);
  } else if (command.command_type == "LASER_SET_OFF" ||
             command.command_type == "LASER_FLASH_STOP") {
    complete = known_off(*laser);
  } else if (command.command_type == "LASER_FLASH_START") {
    complete = laser->machine().flash_active() &&
               !laser->machine().pulse_in_progress();
  } else if (command.command_type == "LASER_STATE_READ" ||
             command.command_type == "LASER_STATE_RESYNC") {
    complete = true;
  }

  if (complete) {
    if (command.command_type == "LASER_FLASH_STOP") {
      state->flash = stagelaser::FlashObservationInfo{};
    }
    mark_result(state, command, "COMPLETED", true);
    const std::string payload =
        command.command_type == "LASER_STATE_READ"
            ? state_result_payload(*laser)
            : "{}";
    return stagelaser::make_command_result(
        device_id, command.command_id, "COMPLETED",
        nullptr, nullptr, nullptr, false, payload);
  }

  state->pending = true;
  state->goal = goal;
  state->command_id = command.command_id;
  state->command_type = command.command_type;
  mark_result(state, command, "ACCEPTED", false);
  return stagelaser::make_command_result(
      device_id, command.command_id, "ACCEPTED");
}

std::string poll_runtime_command(
    const std::string &device_id,
    stagelaser::LaserController *laser,
    RuntimeCommandState *state,
    uint64_t now_ms) {
  if (laser == nullptr || state == nullptr) return {};
  state->response_command_id.clear();

  const stagelaser::ControllerOutcome outcome = laser->Poll(now_ms);
  if (outcome.fault != stagelaser::ControllerFault::kNone) {
    state->controller_faulted = true;
    state->flash = stagelaser::FlashObservationInfo{};
    if (!state->pending) return {};
    const std::string command_id = state->command_id;
    stagelaser::CommandEnvelope command;
    command.command_id = state->command_id;
    command.command_type = state->command_type;
    state->pending = false;
    state->goal = PendingGoal::kNone;
    state->response_command_id = command_id;
    mark_result(state, command, "FAILED", false);
    return failure_for_fault(device_id, command_id, outcome.fault);
  }

  if (state->flash.active && !laser->machine().flash_active()) {
    state->flash = stagelaser::FlashObservationInfo{};
  }
  if (!state->pending) return {};

  if (laser->machine().resync_required()) {
    stagelaser::CommandEnvelope command;
    command.command_id = state->command_id;
    command.command_type = state->command_type;
    const std::string command_id = state->command_id;
    state->pending = false;
    state->goal = PendingGoal::kNone;
    state->flash = stagelaser::FlashObservationInfo{};
    state->response_command_id = command_id;
    mark_result(state, command, "FAILED", false);
    return stagelaser::make_command_result(
        device_id, command_id, "FAILED", "LASER_STATE_UNSAFE", "SAFETY",
        "StageLaser state became unknown while applying the command", false);
  }

  bool complete = false;
  switch (state->goal) {
    case PendingGoal::kOn:
      complete = known_on(*laser);
      break;
    case PendingGoal::kOff:
      complete = known_off(*laser);
      break;
    case PendingGoal::kSafeOff:
      complete = known_safe_off(*laser);
      break;
    case PendingGoal::kFlashStarted:
      complete = laser->machine().flash_active() &&
                 !laser->machine().pulse_in_progress();
      break;
    case PendingGoal::kNone:
      break;
  }
  if (!complete) return {};

  stagelaser::CommandEnvelope command;
  command.command_id = state->command_id;
  command.command_type = state->command_type;
  const std::string command_id = state->command_id;
  const bool stop_flash = state->command_type == "LASER_FLASH_STOP";
  state->pending = false;
  state->goal = PendingGoal::kNone;
  state->command_id.clear();
  state->command_type.clear();
  state->response_command_id = command_id;
  if (stop_flash) state->flash = stagelaser::FlashObservationInfo{};
  mark_result(state, command, "COMPLETED", true);
  return stagelaser::make_command_result(
      device_id, command_id, "COMPLETED",
      nullptr, nullptr, nullptr, false, "{}");
}

}  // namespace

esp_err_t run_stage_device_runtime(
    const VerifiedHub &hub,
    const RuntimeCredential &credential,
    const DeviceIdentity &identity,
    const DeviceConfig &config,
    stagelaser::LaserController *laser) {
  if (hub.certificate_der.empty() || hub.address.empty() || hub.port == 0 ||
      credential.token.empty() || identity.device_id().empty() ||
      !config.complete() || laser == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  RuntimeContext context;
  RuntimeCommandState command_state;
  context.events = xEventGroupCreate();
  context.lock = xSemaphoreCreateMutex();
  context.device_id = identity.device_id();
  if (context.events == nullptr || context.lock == nullptr) {
    if (context.lock != nullptr) vSemaphoreDelete(context.lock);
    if (context.events != nullptr) vEventGroupDelete(context.events);
    return ESP_ERR_NO_MEM;
  }

  char uri[192];
  std::snprintf(uri, sizeof(uri),
                "wss://%s:%u/api/v1/stage-devices/runtime",
                hub.address.c_str(), hub.port);
  const std::string headers =
      "Authorization: StageCoreSession " + credential.token + "\r\n";

  esp_websocket_client_config_t ws_config{};
  ws_config.uri = uri;
  ws_config.disable_auto_reconnect = true;
  ws_config.user_context = &context;
  ws_config.buffer_size = 4096;
  ws_config.cert_pem =
      reinterpret_cast<const char *>(hub.certificate_der.data());
  ws_config.cert_len = hub.certificate_der.size();
  ws_config.subprotocol = kProtocolVersion;
  ws_config.headers = headers.c_str();
  ws_config.skip_cert_common_name_check = true;
  ws_config.network_timeout_ms = 5000;
  ws_config.ping_interval_sec = 10;
  ws_config.pingpong_timeout_sec = 20;
  ws_config.keep_alive_enable = true;
  ws_config.keep_alive_idle = 10;
  ws_config.keep_alive_interval = 5;
  ws_config.keep_alive_count = 3;

  esp_websocket_client_handle_t client =
      esp_websocket_client_init(&ws_config);
  if (client == nullptr) {
    vSemaphoreDelete(context.lock);
    vEventGroupDelete(context.events);
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = esp_websocket_register_events(
      client, WEBSOCKET_EVENT_ANY, &runtime_event_handler, &context);
  if (err != ESP_OK) goto cleanup;

  err = esp_websocket_client_start(client);
  if (err != ESP_OK) goto cleanup;

  {
    const EventBits_t bits = xEventGroupWaitBits(
        context.events, kConnectedBit | kDisconnectedBit | kProtocolErrorBit,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(kReadyTimeoutMs));
    if ((bits & kConnectedBit) == 0) {
      err = (bits & kProtocolErrorBit) ? ESP_ERR_INVALID_RESPONSE
                                       : ESP_ERR_TIMEOUT;
      goto cleanup;
    }
  }

  {
    const std::string hello = make_hello(hub, identity, config, *laser);
    err = send_text(client, hello);
    if (err != ESP_OK) goto cleanup;
  }

  {
    const EventBits_t bits = xEventGroupWaitBits(
        context.events, kAssignmentBit | kDisconnectedBit | kProtocolErrorBit,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(kReadyTimeoutMs));
    if ((bits & kAssignmentBit) == 0) {
      err = (bits & kProtocolErrorBit) ? ESP_ERR_INVALID_RESPONSE
                                       : ESP_ERR_TIMEOUT;
      goto cleanup;
    }
  }

  if (context.assignment_state == "ACTIVE") {
    err = drive_safe_off(laser);
    if (err != ESP_OK) goto cleanup;

    err = send_text(client, make_scope_ack(hub, context, *laser));
    if (err != ESP_OK) goto cleanup;

    const EventBits_t ready_bits = xEventGroupWaitBits(
        context.events,
        kRuntimeReadyBit | kDisconnectedBit | kProtocolErrorBit,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(kReadyTimeoutMs));
    if ((ready_bits & kRuntimeReadyBit) == 0) {
      err = (ready_bits & kProtocolErrorBit)
                ? ESP_ERR_INVALID_RESPONSE
                : ESP_ERR_TIMEOUT;
      goto cleanup;
    }
  }

  {
    int64_t last_observation_us = 0;
    while (true) {
      const EventBits_t bits = xEventGroupGetBits(context.events);
      if (bits & kProtocolErrorBit) {
        err = ESP_ERR_INVALID_RESPONSE;
        break;
      }
      if (bits & kDisconnectedBit) {
        err = ESP_ERR_INVALID_STATE;
        break;
      }

      PrepareRequest prepare;
      if ((bits & kPrepareBit) && take_prepare(&context, &prepare)) {
        err = drive_safe_off(laser);
        if (err != ESP_OK) break;
        const std::string ack =
            make_safe_ack(hub, context.device_id, prepare, *laser);
        err = send_text(client, ack);
        if (err != ESP_OK) break;
      }

      std::string command_frame;
      if ((bits & kCommandBit) && take_command(&context, &command_frame)) {
        stagelaser::CommandDecision decision;
        const esp_err_t command_err =
            stagelaser::evaluate_command_execute_frame(
                command_frame, context.device_id, context.project_id,
                context.runtime_snapshot_id, &context.journal, &decision);
        if (command_err != ESP_OK) {
          err = command_err;
          break;
        }

        if (decision.disposition == stagelaser::CommandDisposition::kReady) {
          const std::string response = start_ready_command(
              decision.command, context.device_id, laser, &command_state,
              esp_timer_get_time() / 1000ULL);
          if (response.empty()) {
            err = ESP_FAIL;
            break;
          }
          context.journal.Remember(decision.command.command_id, response);
          err = send_text(client, response);
        } else {
          err = send_text(client, decision.response_json);
        }
        if (err != ESP_OK) break;
        last_observation_us = 0;
      }

      const uint64_t now_ms =
          static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
      const std::string terminal = poll_runtime_command(
          context.device_id, laser, &command_state, now_ms);
      if (!terminal.empty()) {
        if (!command_state.response_command_id.empty()) {
          context.journal.Remember(
              command_state.response_command_id, terminal);
        }
        err = send_text(client, terminal);
        if (err != ESP_OK) break;
        last_observation_us = 0;
      }
      if (command_state.controller_faulted) {
        err = ESP_ERR_INVALID_STATE;
        break;
      }

      const int64_t now_us = esp_timer_get_time();
      if (last_observation_us == 0 ||
          now_us - last_observation_us >=
              static_cast<int64_t>(kObservationPeriodMs) * 1000LL) {
        err = send_text(
            client, make_observation(
                        hub, context.device_id, *laser, &command_state,
                        context.commands_enabled));
        if (err != ESP_OK) break;
        last_observation_us = now_us;
      }

      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

cleanup:
  if (context.commands_enabled) {
    const esp_err_t safe_err = drive_safe_off(laser);
    if (safe_err != ESP_OK) {
      ESP_LOGE(kTag,
               "runtime ended and deterministic Safe Off could not be proven: %s",
               esp_err_to_name(safe_err));
    }
  }
  if (client != nullptr) {
    (void)esp_websocket_client_stop(client);
    (void)esp_websocket_client_destroy(client);
  }
  if (context.lock != nullptr) vSemaphoreDelete(context.lock);
  if (context.events != nullptr) vEventGroupDelete(context.events);
  return err;
}

}  // namespace stagecore
