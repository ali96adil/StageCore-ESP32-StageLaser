#pragma once

#include <cstdint>
#include <string>

#include "cJSON.h"
#include "laser_state_machine.h"

namespace stagecore::stagelaser {

struct FlashObservationInfo {
  bool active = false;
  std::string command_id;
  double frequency_hz = 0.0;
  uint32_t duration_ms = 0;
  std::string started_at;
  std::string ends_at;
};

struct ObservationMetadata {
  std::string firmware_version;
  std::string build_revision;
  std::string boot_id;
  int64_t uptime_seconds = 0;
  std::string reset_reason;
  int reset_reason_code = -1;
  // Firmware build-time facts, not claims of physical qualification.
  bool actuation_enabled = false;
  bool shared_power_qualified = false;
  bool gpio_no_load_qualification = false;
  int output_gpio = -1;
  bool has_wifi_rssi = false;
  int wifi_rssi_dbm = 0;
  std::string ip_address;
  FlashObservationInfo flash;
  std::string last_accepted_command_id;
  std::string last_applied_command_id;
  std::string last_command_type;
  std::string last_command_result;
};

cJSON *make_observed_state_json(const StateMachine &machine,
                                const ObservationMetadata &metadata);

}  // namespace stagecore::stagelaser
