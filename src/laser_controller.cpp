#include "laser_controller.h"

namespace stagecore::stagelaser {

LaserController::LaserController(const PersistentState &persisted,
                                 ResetClass reset,
                                 bool shared_power_qualified,
                                 PersistentStateSink *store,
                                 RelayActuator *actuator,
                                 Limits limits)
    : machine_(limits), store_(store), actuator_(actuator) {
  machine_.Boot(persisted, reset, shared_power_qualified);
}

bool LaserController::SamePersistent(const PersistentState &a,
                                     const PersistentState &b) {
  return a.stable_state == b.stable_state &&
         a.quality == b.quality &&
         a.interrupted_transition == b.interrupted_transition &&
         a.flash_session_in_progress == b.flash_session_in_progress &&
         a.relay_pulse_count == b.relay_pulse_count;
}

bool LaserController::PersistCurrent() {
  return store_ != nullptr && store_->Save(machine_.PersistentSnapshot());
}

void LaserController::EnterUnknown() {
  PersistentState uncertain;
  uncertain.stable_state = LogicalState::kUnknown;
  uncertain.quality = StateQuality::kUnknown;
  uncertain.relay_pulse_count = machine_.relay_pulse_count();
  machine_.Boot(uncertain, ResetClass::kSoftware, false);
}

ControllerOutcome LaserController::ApplyActuator(
    Decision decision, const PersistentState &before) {
  if (actuator_ == nullptr) {
    EnterUnknown();
    if (store_ != nullptr) (void)PersistCurrent();
    return {decision, ControllerFault::kActuatorPick};
  }

  if (decision.actuator == ActuatorAction::kPick) {
    const PersistentState after = machine_.PersistentSnapshot();

    // Outside an already-persisted Flash session, the transition marker MUST
    // reach durable storage before the dry contact can close.
    if (!before.flash_session_in_progress) {
      if (store_ == nullptr || !store_->Save(after)) {
        machine_.Boot(before, ResetClass::kSoftware, false);
        return {decision, ControllerFault::kPersistence};
      }
    }

    if (!actuator_->Pick()) {
      // Release is an electrical fail-safe attempt. Logical truth is UNKNOWN
      // regardless of whether PICK partially reached the physical contact.
      (void)actuator_->Release();
      EnterUnknown();
      if (store_ != nullptr) (void)PersistCurrent();
      return {decision, ControllerFault::kActuatorPick};
    }
  }
  return {decision, ControllerFault::kNone};
}

ControllerOutcome LaserController::FinishMutation(
    const PersistentState &before, Decision decision) {
  if (decision.actuator != ActuatorAction::kNone) {
    return ApplyActuator(decision, before);
  }

  const PersistentState after = machine_.PersistentSnapshot();
  if (!SamePersistent(before, after)) {
    if (store_ == nullptr || !store_->Save(after)) {
      EnterUnknown();
      if (store_ != nullptr) (void)PersistCurrent();
      return {decision, ControllerFault::kPersistence};
    }
  }
  return {decision, ControllerFault::kNone};
}

ControllerOutcome LaserController::Arm(uint64_t now_ms) {
  const PersistentState before = machine_.PersistentSnapshot();
  return FinishMutation(before, machine_.CommandArm(now_ms));
}

ControllerOutcome LaserController::Disarm(uint64_t now_ms) {
  const PersistentState before = machine_.PersistentSnapshot();
  return FinishMutation(before, machine_.CommandDisarm(now_ms));
}

ControllerOutcome LaserController::SetOn(uint64_t now_ms) {
  const PersistentState before = machine_.PersistentSnapshot();
  return FinishMutation(before, machine_.CommandSetOn(now_ms));
}

ControllerOutcome LaserController::SetOff(uint64_t now_ms) {
  const PersistentState before = machine_.PersistentSnapshot();
  return FinishMutation(before, machine_.CommandSetOff(now_ms));
}

ControllerOutcome LaserController::FlashStart(uint64_t now_ms,
                                              FlashRequest request) {
  const PersistentState before = machine_.PersistentSnapshot();
  return FinishMutation(before, machine_.CommandFlashStart(now_ms, request));
}

ControllerOutcome LaserController::FlashStop(uint64_t now_ms) {
  const PersistentState before = machine_.PersistentSnapshot();
  return FinishMutation(before, machine_.CommandFlashStop(now_ms));
}

ControllerOutcome LaserController::SafeOff(uint64_t now_ms) {
  const PersistentState before = machine_.PersistentSnapshot();
  return FinishMutation(before, machine_.CommandSafeOff(now_ms));
}

ControllerOutcome LaserController::ResyncOff() {
  const PersistentState before = machine_.PersistentSnapshot();
  return FinishMutation(before, machine_.CommandResyncOff());
}

ControllerOutcome LaserController::ResyncOn() {
  const PersistentState before = machine_.PersistentSnapshot();
  return FinishMutation(before, machine_.CommandResyncOn());
}

ControllerOutcome LaserController::Poll(uint64_t now_ms) {
  const PersistentState before = machine_.PersistentSnapshot();
  Decision decision = machine_.Tick(now_ms);

  if (decision.actuator == ActuatorAction::kPick) {
    return ApplyActuator(decision, before);
  }

  if (decision.actuator == ActuatorAction::kRelease) {
    if (actuator_ == nullptr || !actuator_->Release()) {
      machine_.ConfirmRelease(false, now_ms);
      EnterUnknown();
      if (store_ != nullptr) (void)PersistCurrent();
      return {decision, ControllerFault::kActuatorRelease};
    }

    machine_.ConfirmRelease(true, now_ms);
    const PersistentState after = machine_.PersistentSnapshot();

    // While Flash remains active, its session marker was durably written once
    // at start. Do not wear NVS by rewriting every local phase.
    if (!machine_.flash_active() && !SamePersistent(before, after)) {
      if (store_ == nullptr || !store_->Save(after)) {
        EnterUnknown();
        return {decision, ControllerFault::kPersistence};
      }
    }
    return {decision, ControllerFault::kNone};
  }

  const PersistentState after = machine_.PersistentSnapshot();
  if (!SamePersistent(before, after)) {
    if (store_ == nullptr || !store_->Save(after)) {
      EnterUnknown();
      return {decision, ControllerFault::kPersistence};
    }
  }
  return {decision, ControllerFault::kNone};
}

}  // namespace stagecore::stagelaser
