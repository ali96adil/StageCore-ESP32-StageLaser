#pragma once

#include <cstdint>

namespace stagecore::stagelaser {

constexpr const char *kProfileId = "stagecore.esp32-stagelaser";
constexpr const char *kControlContract = "stagecore.stagelaser/1";

constexpr uint32_t kDefaultPulseMs = 180;
constexpr uint32_t kDefaultMinRestMs = 250;
constexpr double kDefaultMinFlashHz = 0.1;
constexpr double kDefaultMaxFlashHz = 1.0;
constexpr uint32_t kDefaultMaxFlashDurationMs = 60000;

enum class ArmState { kDisarmed, kArmed };
enum class LogicalState {
  kOff,
  kTurningOn,
  kOn,
  kTurningOff,
  kFlashOn,
  kFlashOff,
  kUnknown,
  kError,
};
enum class StateQuality { kTracked, kConfirmed, kUnknown };
enum class ResetClass { kSoftware, kWatchdog, kPowerOn, kBrownout, kUnknown };
enum class Command {
  kArm,
  kDisarm,
  kSetOn,
  kSetOff,
  kFlashStart,
  kFlashStop,
  kSafeOff,
  kStateRead,
  kResyncOff,
  kResyncOn,
};
enum class ActuatorAction { kNone, kPick, kRelease };
enum class ResultCode {
  kAccepted,
  kNoop,
  kRejectedDisarmed,
  kRejectedUnknown,
  kRejectedBusy,
  kRejectedLimits,
  kRejectedUnsafe,
};

struct Limits {
  uint32_t pulse_ms = kDefaultPulseMs;
  uint32_t min_rest_ms = kDefaultMinRestMs;
  double min_flash_hz = kDefaultMinFlashHz;
  double max_flash_hz = kDefaultMaxFlashHz;
  uint32_t max_flash_duration_ms = kDefaultMaxFlashDurationMs;
};

struct FlashRequest {
  double frequency_hz = 1.0;
  uint32_t duration_ms = 8000;
};

struct Decision {
  ResultCode result = ResultCode::kAccepted;
  ActuatorAction actuator = ActuatorAction::kNone;
};

struct PersistentState {
  LogicalState stable_state = LogicalState::kUnknown;
  StateQuality quality = StateQuality::kUnknown;
  bool interrupted_transition = false;
  bool flash_session_in_progress = false;
  uint64_t relay_pulse_count = 0;
};

}  // namespace stagecore::stagelaser
