#include "laser_observation.h"

#include "laser_contract.h"

namespace stagecore::stagelaser {
namespace {

const char *arm_text(ArmState value) {
  return value == ArmState::kArmed ? "ARMED" : "DISARMED";
}

const char *logical_text(LogicalState value) {
  switch (value) {
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

const char *quality_text(StateQuality value) {
  switch (value) {
    case StateQuality::kTracked: return "TRACKED";
    case StateQuality::kConfirmed: return "CONFIRMED";
    case StateQuality::kUnknown:
    default: return "UNKNOWN";
  }
}

void add_optional_string(cJSON *root, const char *key,
                         const std::string &value) {
  if (!value.empty()) cJSON_AddStringToObject(root, key, value.c_str());
}

}  // namespace

cJSON *make_observed_state_json(const StateMachine &machine,
                                const ObservationMetadata &metadata) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return nullptr;

  cJSON_AddNumberToObject(root, "schema_version", 1);
  add_optional_string(root, "firmware_version", metadata.firmware_version);
  cJSON_AddStringToObject(root, "control_contract_version", kControlContract);
  add_optional_string(root, "boot_id", metadata.boot_id);
  if (metadata.uptime_seconds >= 0) {
    cJSON_AddNumberToObject(root, "uptime_seconds",
                            static_cast<double>(metadata.uptime_seconds));
  }
  add_optional_string(root, "reset_reason", metadata.reset_reason);
  if (metadata.has_wifi_rssi) {
    cJSON_AddNumberToObject(root, "wifi_rssi_dbm", metadata.wifi_rssi_dbm);
  }
  add_optional_string(root, "ip_address", metadata.ip_address);

  cJSON_AddStringToObject(root, "arm_state", arm_text(machine.arm_state()));
  cJSON_AddStringToObject(root, "logical_state",
                          logical_text(machine.logical_state()));
  cJSON_AddStringToObject(root, "state_quality",
                          quality_text(machine.state_quality()));
  cJSON_AddBoolToObject(root, "resync_required", machine.resync_required());
  cJSON_AddBoolToObject(root, "pulse_in_progress",
                        machine.pulse_in_progress());
  cJSON_AddNumberToObject(root, "relay_pulse_count",
                          static_cast<double>(machine.relay_pulse_count()));
  cJSON_AddStringToObject(root, "driver_kind", "MECHANICAL_RELAY");

  const Limits &limits = machine.limits();
  cJSON *limits_json = cJSON_CreateObject();
  if (limits_json == nullptr) {
    cJSON_Delete(root);
    return nullptr;
  }
  cJSON_AddNumberToObject(limits_json, "pulse_ms", limits.pulse_ms);
  cJSON_AddNumberToObject(limits_json, "minimum_rest_ms", limits.min_rest_ms);
  cJSON_AddNumberToObject(limits_json, "minimum_flash_hz", limits.min_flash_hz);
  cJSON_AddNumberToObject(limits_json, "maximum_flash_hz", limits.max_flash_hz);
  cJSON_AddNumberToObject(limits_json, "maximum_duration_ms",
                          limits.max_flash_duration_ms);
  cJSON_AddItemToObject(root, "limits", limits_json);

  if (metadata.flash.active) {
    cJSON *flash = cJSON_CreateObject();
    if (flash == nullptr) {
      cJSON_Delete(root);
      return nullptr;
    }
    cJSON_AddStringToObject(flash, "command_id",
                            metadata.flash.command_id.c_str());
    cJSON_AddNumberToObject(flash, "frequency_hz",
                            metadata.flash.frequency_hz);
    cJSON_AddNumberToObject(flash, "duration_ms",
                            metadata.flash.duration_ms);
    add_optional_string(flash, "started_at", metadata.flash.started_at);
    add_optional_string(flash, "ends_at", metadata.flash.ends_at);
    cJSON_AddItemToObject(root, "active_flash", flash);
  }

  add_optional_string(root, "last_accepted_command_id",
                      metadata.last_accepted_command_id);
  add_optional_string(root, "last_applied_command_id",
                      metadata.last_applied_command_id);
  add_optional_string(root, "last_command_type",
                      metadata.last_command_type);
  add_optional_string(root, "last_command_result",
                      metadata.last_command_result);
  return root;
}

}  // namespace stagecore::stagelaser
