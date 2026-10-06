#pragma once

#include <cstdint>
#include "laser_contract.h"

namespace stagecore::stagelaser {

class StateMachine {
 public:
  explicit StateMachine(Limits limits = {});

  void Boot(const PersistentState &persisted, ResetClass reset,
            bool shared_power_qualified);
  Decision CommandArm(uint64_t now_ms);
  Decision CommandDisarm(uint64_t now_ms);
  Decision CommandSetOn(uint64_t now_ms);
  Decision CommandSetOff(uint64_t now_ms);
  Decision CommandFlashStart(uint64_t now_ms, FlashRequest request);
  Decision CommandFlashStop(uint64_t now_ms);
  Decision CommandSafeOff(uint64_t now_ms);
  Decision CommandResyncOff();
  Decision CommandResyncOn();
  Decision Tick(uint64_t now_ms);

  // Must be called only after the hardware driver has actually released the
  // contact. Stable state is never committed at PICK time.
  void ConfirmRelease(bool release_succeeded, uint64_t now_ms);

  ArmState arm_state() const { return arm_; }
  LogicalState logical_state() const { return logical_; }
  StateQuality state_quality() const { return quality_; }
  bool resync_required() const { return quality_ == StateQuality::kUnknown || logical_ == LogicalState::kUnknown || logical_ == LogicalState::kError; }
  bool pulse_in_progress() const { return pulse_in_progress_; }
  bool flash_active() const { return flash_active_; }
  bool safe_off_pending() const { return safe_off_pending_; }
  uint64_t relay_pulse_count() const { return relay_pulse_count_; }
  PersistentState PersistentSnapshot() const;
  const Limits &limits() const { return limits_; }

 private:
  Decision BeginPulse(bool target_on, uint64_t now_ms, bool from_flash);
  Decision RequestKnownOff(uint64_t now_ms, bool disarm_first);
  void ScheduleNextFlashEdge(uint64_t now_ms);
  bool KnownStableOff() const;
  bool KnownStableOn() const;
  bool KnownStable() const;
  LogicalState StableLogical() const;

  Limits limits_;
  ArmState arm_ = ArmState::kDisarmed;
  LogicalState logical_ = LogicalState::kUnknown;
  StateQuality quality_ = StateQuality::kUnknown;
  bool pulse_in_progress_ = false;
  bool release_requested_ = false;
  bool pending_target_on_ = false;
  bool pulse_from_flash_ = false;
  uint64_t release_due_ms_ = 0;
  uint64_t rest_until_ms_ = 0;
  uint64_t relay_pulse_count_ = 0;

  bool flash_active_ = false;
  bool flash_stop_requested_ = false;
  bool safe_off_pending_ = false;
  uint64_t flash_end_ms_ = 0;
  uint64_t flash_next_edge_ms_ = 0;
  uint64_t flash_half_period_ms_ = 500;
};

}  // namespace stagecore::stagelaser
