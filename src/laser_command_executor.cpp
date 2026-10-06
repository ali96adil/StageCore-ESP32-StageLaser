#include "laser_command_executor.h"

namespace stagecore::stagelaser {
namespace {

bool known_quality(StateQuality quality) {
  return quality == StateQuality::kTracked ||
         quality == StateQuality::kConfirmed;
}

}  // namespace

bool CommandExecutor::IsSafetyPriority(const std::string &command_type) {
  return command_type == "LASER_SAFE_OFF" ||
         command_type == "LASER_DISARM";
}

bool CommandExecutor::IsRejected(ResultCode result) {
  switch (result) {
    case ResultCode::kRejectedDisarmed:
    case ResultCode::kRejectedUnknown:
    case ResultCode::kRejectedBusy:
    case ResultCode::kRejectedLimits:
    case ResultCode::kRejectedUnsafe:
      return true;
    case ResultCode::kAccepted:
    case ResultCode::kNoop:
    default:
      return false;
  }
}

ExecutionEvent CommandExecutor::Accepted(const std::string &command_id) {
  return {ExecutionEventKind::kAccepted, command_id};
}

ExecutionEvent CommandExecutor::Completed(const std::string &command_id,
                                          bool include_state) {
  ExecutionEvent event{ExecutionEventKind::kCompleted, command_id};
  event.include_state_payload = include_state;
  return event;
}

ExecutionEvent CommandExecutor::Rejected(const std::string &command_id,
                                         ResultCode result) {
  ExecutionEvent event{ExecutionEventKind::kRejected, command_id};
  event.retryable = false;
  switch (result) {
    case ResultCode::kRejectedDisarmed:
      event.error_code = "LASER_NOT_ARMED";
      event.error_category = "SAFETY";
      event.message = "StageLaser must be ARMED before this command";
      break;
    case ResultCode::kRejectedUnknown:
    case ResultCode::kRejectedUnsafe:
      event.error_code = "LASER_STATE_UNKNOWN";
      event.error_category = "SAFETY";
      event.message = "StageLaser state is not known well enough for this command";
      break;
    case ResultCode::kRejectedBusy:
      event.error_code = "LASER_BUSY";
      event.error_category = "RUNTIME";
      event.message = "StageLaser is completing another local transition";
      event.retryable = true;
      break;
    case ResultCode::kRejectedLimits:
      event.error_code = "LASER_LIMITS";
      event.error_category = "VALIDATION";
      event.message = "StageLaser command exceeds local safety limits";
      break;
    default:
      event.error_code = "DEVICE_COMMAND_REJECTED";
      event.error_category = "RUNTIME";
      event.message = "StageLaser rejected the command";
      break;
  }
  return event;
}

ExecutionEvent CommandExecutor::Failed(const std::string &command_id,
                                       ControllerFault fault) {
  ExecutionEvent event{ExecutionEventKind::kFailed, command_id};
  event.error_category = "HARDWARE";
  event.retryable = false;
  switch (fault) {
    case ControllerFault::kPersistence:
      event.error_code = "LASER_STATE_PERSISTENCE_FAILED";
      event.message = "StageLaser could not persist physical truth state";
      break;
    case ControllerFault::kActuatorPick:
      event.error_code = "LASER_ACTUATOR_PICK_FAILED";
      event.message = "StageLaser dry-contact PICK failed";
      break;
    case ControllerFault::kActuatorRelease:
      event.error_code = "LASER_ACTUATOR_RELEASE_FAILED";
      event.message = "StageLaser dry-contact RELEASE failed";
      break;
    case ControllerFault::kNone:
    default:
      event.error_code = "LASER_EXECUTION_FAILED";
      event.message = "StageLaser command execution failed";
      break;
  }
  return event;
}

bool CommandExecutor::GoalReached(Goal goal) const {
  if (laser_ == nullptr) return false;
  const StateMachine &machine = laser_->machine();
  const bool stable =
      !machine.pulse_in_progress() &&
      known_quality(machine.state_quality()) &&
      !machine.resync_required();

  switch (goal) {
    case Goal::kOn:
      return stable && machine.logical_state() == LogicalState::kOn;
    case Goal::kOff:
      return stable && machine.logical_state() == LogicalState::kOff;
    case Goal::kSafeOff:
      return stable &&
             machine.arm_state() == ArmState::kDisarmed &&
             machine.logical_state() == LogicalState::kOff &&
             !machine.flash_active() &&
             !machine.safe_off_pending();
    case Goal::kFlashStarted:
      return stable && machine.flash_active() &&
             (machine.logical_state() == LogicalState::kFlashOn ||
              machine.logical_state() == LogicalState::kFlashOff);
    case Goal::kFlashStopped:
      return stable && !machine.flash_active() &&
             machine.logical_state() == LogicalState::kOff;
    case Goal::kNone:
    default:
      return true;
  }
}

ExecutionBatch CommandExecutor::Start(const CommandEnvelope &command,
                                      uint64_t now_ms) {
  ExecutionBatch batch;
  if (laser_ == nullptr || command.command_id.empty()) {
    batch.Push(Failed(command.command_id, ControllerFault::kNone));
    return batch;
  }

  ControllerOutcome outcome;
  Goal goal = Goal::kNone;
  bool include_state = false;

  if (command.command_type == "LASER_ARM") {
    outcome = laser_->Arm(now_ms);
  } else if (command.command_type == "LASER_DISARM") {
    outcome = laser_->Disarm(now_ms);
    goal = Goal::kSafeOff;
  } else if (command.command_type == "LASER_SET_ON") {
    outcome = laser_->SetOn(now_ms);
    goal = Goal::kOn;
  } else if (command.command_type == "LASER_SET_OFF") {
    outcome = laser_->SetOff(now_ms);
    goal = Goal::kOff;
  } else if (command.command_type == "LASER_FLASH_START") {
    outcome = laser_->FlashStart(
        now_ms,
        FlashRequest{command.flash_frequency_hz, command.flash_duration_ms});
    goal = Goal::kFlashStarted;
  } else if (command.command_type == "LASER_FLASH_STOP") {
    outcome = laser_->FlashStop(now_ms);
    goal = Goal::kFlashStopped;
  } else if (command.command_type == "LASER_SAFE_OFF") {
    outcome = laser_->SafeOff(now_ms);
    goal = Goal::kSafeOff;
  } else if (command.command_type == "LASER_STATE_READ") {
    batch.Push(Completed(command.command_id, true));
    return batch;
  } else if (command.command_type == "LASER_STATE_RESYNC") {
    if (command.resync_state == "OFF") {
      outcome = laser_->ResyncOff();
    } else if (command.resync_state == "ON") {
      outcome = laser_->ResyncOn();
    } else {
      batch.Push(Rejected(command.command_id,
                          ResultCode::kRejectedLimits));
      return batch;
    }
    include_state = true;
  } else {
    batch.Push(Rejected(command.command_id,
                        ResultCode::kRejectedLimits));
    return batch;
  }

  if (outcome.fault != ControllerFault::kNone) {
    batch.Push(Failed(command.command_id, outcome.fault));
    return batch;
  }
  if (IsRejected(outcome.decision.result)) {
    batch.Push(Rejected(command.command_id, outcome.decision.result));
    return batch;
  }

  if (goal == Goal::kNone || GoalReached(goal)) {
    batch.Push(Completed(command.command_id, include_state));
    return batch;
  }

  pending_ = true;
  pending_command_id_ = command.command_id;
  pending_goal_ = goal;
  batch.Push(Accepted(command.command_id));
  return batch;
}

ExecutionBatch CommandExecutor::Begin(const CommandEnvelope &command,
                                      uint64_t now_ms) {
  ExecutionBatch batch;

  if (pending_) {
    if (!IsSafetyPriority(command.command_type)) {
      ExecutionEvent busy{ExecutionEventKind::kRejected, command.command_id};
      busy.error_code = "LASER_BUSY";
      busy.error_category = "RUNTIME";
      busy.message = "Another StageLaser transition is still pending";
      busy.retryable = true;
      batch.Push(std::move(busy));
      return batch;
    }

    ExecutionEvent preempted{ExecutionEventKind::kFailed,
                             pending_command_id_};
    preempted.error_code = "LASER_COMMAND_PREEMPTED";
    preempted.error_category = "SAFETY";
    preempted.message =
        "StageLaser command was preempted by DISARM/Safe Off";
    preempted.retryable = false;
    batch.Push(std::move(preempted));
    pending_ = false;
    pending_command_id_.clear();
    pending_goal_ = Goal::kNone;
  }

  ExecutionBatch started = Start(command, now_ms);
  for (size_t i = 0; i < started.count; ++i) {
    batch.Push(std::move(started.events[i]));
  }
  return batch;
}

ExecutionBatch CommandExecutor::Poll(uint64_t now_ms) {
  ExecutionBatch batch;
  if (laser_ == nullptr) return batch;

  const ControllerOutcome outcome = laser_->Poll(now_ms);
  if (outcome.fault != ControllerFault::kNone) {
    if (pending_) {
      batch.Push(Failed(pending_command_id_, outcome.fault));
      pending_ = false;
      pending_command_id_.clear();
      pending_goal_ = Goal::kNone;
    }
    return batch;
  }

  if (pending_ && IsRejected(outcome.decision.result)) {
    batch.Push(Rejected(pending_command_id_, outcome.decision.result));
    pending_ = false;
    pending_command_id_.clear();
    pending_goal_ = Goal::kNone;
    return batch;
  }

  if (pending_ && GoalReached(pending_goal_)) {
    batch.Push(Completed(pending_command_id_));
    pending_ = false;
    pending_command_id_.clear();
    pending_goal_ = Goal::kNone;
  }
  return batch;
}

}  // namespace stagecore::stagelaser
