#include "stage_device_runtime.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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

struct PrepareRequest {
  bool present = false;
  std::string assignment_id;
  int64_t assignment_epoch = 0;
  int64_t connection_generation = 0;
  std::string challenge;
  std::string target_project_id;
  std::string target_runtime_snapshot_id;
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

stagelaser::ObservationMetadata make_observation_metadata() {
  stagelaser::ObservationMetadata metadata;
  metadata.firmware_version = STAGECORE_FW_VERSION;
  metadata.uptime_seconds = esp_timer_get_time() / 1000000LL;
  metadata.reset_reason = reset_reason_text();

  wifi_ap_record_t ap{};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    metadata.has_wifi_rssi = true;
    metadata.wifi_rssi_dbm = ap.rssi;
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
                             const stagelaser::LaserController &laser) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return {};
  cJSON_AddStringToObject(root, "type", "device.observation");
  cJSON_AddNumberToObject(root, "schema_version", 2);
  cJSON_AddStringToObject(root, "device_id", device_id.c_str());
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
  } else {
    // Assignment-only slice: no scope acknowledgement and no command
    // execution authority is accepted here.
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

}  // namespace

esp_err_t run_stage_device_assignment_runtime(
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

      const int64_t now_us = esp_timer_get_time();
      if (last_observation_us == 0 ||
          now_us - last_observation_us >=
              static_cast<int64_t>(kObservationPeriodMs) * 1000LL) {
        err = send_text(client,
                        make_observation(hub, context.device_id, *laser));
        if (err != ESP_OK) break;
        last_observation_us = now_us;
      }

      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }

cleanup:
  if (client != nullptr) {
    (void)esp_websocket_client_stop(client);
    (void)esp_websocket_client_destroy(client);
  }
  if (context.lock != nullptr) vSemaphoreDelete(context.lock);
  if (context.events != nullptr) vEventGroupDelete(context.events);
  return err;
}

}  // namespace stagecore
