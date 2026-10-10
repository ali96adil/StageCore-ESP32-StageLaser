#include "laser_state_machine.h"

#include <algorithm>
#include <cmath>

namespace stagecore::stagelaser {

StateMachine::StateMachine(Limits limits) : limits_(limits) {}

void StateMachine::Boot(const PersistentState &persisted, ResetClass reset,
                        bool shared_power_qualified) {
  arm_ = ArmState::kDisarmed;
  relay_pulse_count_ = persisted.relay_pulse_count;
  pulse_in_progress_ = false;
  release_requested_ = false;
  flash_active_ = false;
  flash_stop_requested_ = false;
  safe_off_pending_ = false;

  if (persisted.interrupted_transition || persisted.flash_session_in_progress) {
    logical_ = LogicalState::kUnknown;
    quality_ = StateQuality::kUnknown;
    return;
  }

  const bool stable =
      (persisted.stable_state == LogicalState::kOff ||
       persisted.stable_state == LogicalState::kOn) &&
      (persisted.quality == StateQuality::kTracked ||
       persisted.quality == StateQuality::kConfirmed);

  if ((reset == ResetClass::kSoftware || reset == ResetClass::kWatchdog) &&
      stable) {
    logical_ = persisted.stable_state;
    quality_ = persisted.quality;
    return;
  }

  if ((reset == ResetClass::kPowerOn || reset == ResetClass::kBrownout) &&
      shared_power_qualified) {
    logical_ = LogicalState::kOff;
    quality_ = StateQuality::kTracked;
    return;
  }

  logical_ = LogicalState::kUnknown;
  quality_ = StateQuality::kUnknown;
}

bool StateMachine::KnownStableOff() const {
  const bool off = logical_ == LogicalState::kOff || logical_ == LogicalState::kFlashOff;
  return !pulse_in_progress_ && off &&
         (quality_ == StateQuality::kTracked || quality_ == StateQuality::kConfirmed);
}

bool StateMachine::KnownStableOn() const {
  const bool on = logical_ == LogicalState::kOn || logical_ == LogicalState::kFlashOn;
  return !pulse_in_progress_ && on &&
         (quality_ == StateQuality::kTracked || quality_ == StateQuality::kConfirmed);
}

bool StateMachine::KnownStable() const { return KnownStableOff() || KnownStableOn(); }
LogicalState StateMachine::StableLogical() const { return KnownStableOn() ? LogicalState::kOn : LogicalState::kOff; }

Decision StateMachine::CommandArm(uint64_t) {
  if (!KnownStable()) return {ResultCode::kRejectedUnknown, ActuatorAction::kNone};
  if (pulse_in_progress_ || flash_active_ || safe_off_pending_)
    return {ResultCode::kRejectedBusy, ActuatorAction::kNone};
  if (arm_ == ArmState::kArmed) return {ResultCode::kNoop, ActuatorAction::kNone};
  arm_ = ArmState::kArmed;
  return {ResultCode::kAccepted, ActuatorAction::kNone};
}

Decision StateMachine::CommandDisarm(uint64_t now_ms) {
  arm_ = ArmState::kDisarmed;
  flash_stop_requested_ = true;
  return RequestKnownOff(now_ms, true);
}

Decision StateMachine::CommandSetOn(uint64_t now_ms) {
  if (arm_ != ArmState::kArmed) return {ResultCode::kRejectedDisarmed, ActuatorAction::kNone};
  if (!KnownStable()) return {ResultCode::kRejectedUnknown, ActuatorAction::kNone};
  if (flash_active_ || pulse_in_progress_) return {ResultCode::kRejectedBusy, ActuatorAction::kNone};
  if (KnownStableOn()) return {ResultCode::kNoop, ActuatorAction::kNone};
  return BeginPulse(true, now_ms, false);
}

Decision StateMachine::CommandSetOff(uint64_t now_ms) {
  if (!KnownStable()) return {ResultCode::kRejectedUnknown, ActuatorAction::kNone};
  if (flash_active_ || pulse_in_progress_) return {ResultCode::kRejectedBusy, ActuatorAction::kNone};
  if (KnownStableOff()) return {ResultCode::kNoop, ActuatorAction::kNone};
  return BeginPulse(false, now_ms, false);
}

Decision StateMachine::CommandFlashStart(uint64_t now_ms, FlashRequest request) {
  if (arm_ != ArmState::kArmed) return {ResultCode::kRejectedDisarmed, ActuatorAction::kNone};
  if (!KnownStable()) return {ResultCode::kRejectedUnknown, ActuatorAction::kNone};
  if (pulse_in_progress_ || flash_active_) return {ResultCode::kRejectedBusy, ActuatorAction::kNone};
  if (!std::isfinite(request.frequency_hz) || request.frequency_hz < limits_.min_flash_hz ||
      request.frequency_hz > limits_.max_flash_hz || request.duration_ms == 0 ||
      request.duration_ms > limits_.max_flash_duration_ms) {
    return {ResultCode::kRejectedLimits, ActuatorAction::kNone};
  }
  flash_active_ = true;
  flash_stop_requested_ = false;
  flash_end_ms_ = now_ms + request.duration_ms;
  flash_half_period_ms_ = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(500.0 / request.frequency_hz)));
  if (KnownStableOff()) return BeginPulse(true, now_ms, true);
  logical_ = LogicalState::kFlashOn;
  ScheduleNextFlashEdge(now_ms);
  return {ResultCode::kAccepted, ActuatorAction::kNone};
}

Decision StateMachine::CommandFlashStop(uint64_t now_ms) {
  if (!flash_active_ && !pulse_in_progress_) {
    if (!KnownStable()) return {ResultCode::kRejectedUnknown, ActuatorAction::kNone};
    if (KnownStableOff()) return {ResultCode::kNoop, ActuatorAction::kNone};
    return BeginPulse(false, now_ms, false);
  }
  flash_stop_requested_ = true;
  if (pulse_in_progress_) return {ResultCode::kAccepted, ActuatorAction::kNone};
  if (KnownStableOff()) {
    flash_active_ = false;
    logical_ = LogicalState::kOff;
    return {ResultCode::kAccepted, ActuatorAction::kNone};
  }
  if (KnownStableOn() && now_ms >= rest_until_ms_) return BeginPulse(false, now_ms, true);
  return {ResultCode::kAccepted, ActuatorAction::kNone};
}

Decision StateMachine::CommandSafeOff(uint64_t now_ms) {
  arm_ = ArmState::kDisarmed;
  flash_stop_requested_ = true;
  return RequestKnownOff(now_ms, true);
}

Decision StateMachine::CommandResyncOff() {
  if (arm_ != ArmState::kDisarmed || pulse_in_progress_ || flash_active_ ||
      safe_off_pending_)
    return {ResultCode::kRejectedBusy, ActuatorAction::kNone};
  logical_ = LogicalState::kOff;
  quality_ = StateQuality::kTracked;
  return {ResultCode::kAccepted, ActuatorAction::kNone};
}

Decision StateMachine::CommandResyncOn() {
  if (arm_ != ArmState::kDisarmed || pulse_in_progress_ || flash_active_ ||
      safe_off_pending_)
    return {ResultCode::kRejectedBusy, ActuatorAction::kNone};
  logical_ = LogicalState::kOn;
  quality_ = StateQuality::kTracked;
  return {ResultCode::kAccepted, ActuatorAction::kNone};
}

Decision StateMachine::BeginPulse(bool target_on, uint64_t now_ms, bool from_flash) {
  if (pulse_in_progress_ || now_ms < rest_until_ms_) return {ResultCode::kRejectedBusy, ActuatorAction::kNone};
  if (!KnownStable()) return {ResultCode::kRejectedUnknown, ActuatorAction::kNone};
  if ((target_on && KnownStableOn()) || (!target_on && KnownStableOff())) return {ResultCode::kNoop, ActuatorAction::kNone};
  pending_target_on_ = target_on;
  pulse_from_flash_ = from_flash;
  pulse_in_progress_ = true;
  // Count a Flash half-cycle from relay pick, not from relay release.
  // Otherwise the 180 ms pick/release pulse length is added to every
  // requested interval (1 Hz becomes roughly 0.74 Hz). The existing
  // min_rest_ms guard still prevents early actuation.
  if (from_flash && flash_active_) ScheduleNextFlashEdge(now_ms);
  release_requested_ = false;
  release_due_ms_ = now_ms + limits_.pulse_ms;
  logical_ = target_on ? LogicalState::kTurningOn : LogicalState::kTurningOff;
  return {ResultCode::kAccepted, ActuatorAction::kPick};
}

Decision StateMachine::RequestKnownOff(uint64_t now_ms, bool) {
  if (pulse_in_progress_) {
    safe_off_pending_ = true;
    return {ResultCode::kAccepted, ActuatorAction::kNone};
  }
  if (!KnownStable()) {
    safe_off_pending_ = false;
    flash_stop_requested_ = false;
    return {ResultCode::kRejectedUnsafe, ActuatorAction::kNone};
  }
  if (KnownStableOff()) {
    safe_off_pending_ = false;
    flash_active_ = false;
    flash_stop_requested_ = false;
    return {ResultCode::kNoop, ActuatorAction::kNone};
  }
  safe_off_pending_ = true;
  if (now_ms < rest_until_ms_) {
    return {ResultCode::kAccepted, ActuatorAction::kNone};
  }
  return BeginPulse(false, now_ms, flash_active_);
}

void StateMachine::ScheduleNextFlashEdge(uint64_t now_ms) {
  flash_next_edge_ms_ = now_ms + flash_half_period_ms_;
}

Decision StateMachine::Tick(uint64_t now_ms) {
  if (pulse_in_progress_) {
    if (!release_requested_ && now_ms >= release_due_ms_) {
      release_requested_ = true;
      return {ResultCode::kAccepted, ActuatorAction::kRelease};
    }
    return {ResultCode::kAccepted, ActuatorAction::kNone};
  }

  if (safe_off_pending_) {
    if (!KnownStable()) {
      safe_off_pending_ = false;
      return {ResultCode::kRejectedUnsafe, ActuatorAction::kNone};
    }
    if (KnownStableOff()) {
      safe_off_pending_ = false;
      flash_active_ = false;
      flash_stop_requested_ = false;
      logical_ = LogicalState::kOff;
      return {ResultCode::kAccepted, ActuatorAction::kNone};
    }
    if (KnownStableOn() && now_ms >= rest_until_ms_) {
      return BeginPulse(false, now_ms, flash_active_);
    }
    return {ResultCode::kAccepted, ActuatorAction::kNone};
  }

  if (flash_active_) {
    if (now_ms >= flash_end_ms_) flash_stop_requested_ = true;
    if (flash_stop_requested_) {
      if (KnownStableOff()) {
        flash_active_ = false;
        flash_stop_requested_ = false;
        logical_ = LogicalState::kOff;
        return {ResultCode::kAccepted, ActuatorAction::kNone};
      }
      if (KnownStableOn() && now_ms >= rest_until_ms_) return BeginPulse(false, now_ms, true);
      return {ResultCode::kAccepted, ActuatorAction::kNone};
    }
    if (now_ms >= flash_next_edge_ms_ && now_ms >= rest_until_ms_) {
      if (KnownStableOff()) return BeginPulse(true, now_ms, true);
      if (KnownStableOn()) return BeginPulse(false, now_ms, true);
    }
  }
  return {ResultCode::kAccepted, ActuatorAction::kNone};
}

void StateMachine::ConfirmRelease(bool release_succeeded, uint64_t now_ms) {
  if (!pulse_in_progress_ || !release_requested_) return;
  pulse_in_progress_ = false;
  release_requested_ = false;
  rest_until_ms_ = now_ms + limits_.min_rest_ms;
  ++relay_pulse_count_;
  if (!release_succeeded) {
    logical_ = LogicalState::kUnknown;
    quality_ = StateQuality::kUnknown;
    flash_active_ = false;
    flash_stop_requested_ = false;
    safe_off_pending_ = false;
    return;
  }
  logical_ = pending_target_on_ ? LogicalState::kOn : LogicalState::kOff;
  quality_ = StateQuality::kTracked;

  if (!pending_target_on_ && safe_off_pending_) {
    safe_off_pending_ = false;
    flash_active_ = false;
    flash_stop_requested_ = false;
    logical_ = LogicalState::kOff;
    return;
  }

  if (pulse_from_flash_ && flash_active_) {
    logical_ = pending_target_on_ ? LogicalState::kFlashOn : LogicalState::kFlashOff;
    if (flash_stop_requested_ && !pending_target_on_) {
      flash_active_ = false;
      flash_stop_requested_ = false;
      logical_ = LogicalState::kOff;
      return;
    }
    // Next Flash edge was scheduled when the pulse was picked.
  }
}

PersistentState StateMachine::PersistentSnapshot() const {
  PersistentState out;
  out.relay_pulse_count = relay_pulse_count_;
  out.interrupted_transition = pulse_in_progress_;
  out.flash_session_in_progress = flash_active_;
  if (pulse_in_progress_) {
    out.stable_state = LogicalState::kUnknown;
    out.quality = StateQuality::kUnknown;
    return out;
  }
  if (logical_ == LogicalState::kOff || logical_ == LogicalState::kOn) {
    out.stable_state = logical_;
  } else if (logical_ == LogicalState::kFlashOff) {
    out.stable_state = LogicalState::kOff;
  } else if (logical_ == LogicalState::kFlashOn) {
    out.stable_state = LogicalState::kOn;
  } else {
    out.stable_state = LogicalState::kUnknown;
  }
  out.quality = quality_;
  return out;
}

}  // namespace stagecore::stagelaser
