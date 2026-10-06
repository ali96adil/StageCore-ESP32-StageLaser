#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "laser_command_contract.h"
#include "laser_controller.h"

namespace stagecore::stagelaser {

enum class ExecutionEventKind {
  kAccepted,
  kCompleted,
  kRejected,
  kFailed,
};

struct ExecutionEvent {
  ExecutionEventKind kind = ExecutionEventKind::kFailed;
  std::string command_id;
  std::string error_code;
  std::string error_category;
  std::string message;
  bool retryable = false;
  bool include_state_payload = false;
};

struct ExecutionBatch {
  std::array<ExecutionEvent, 2> events{};
  size_t count = 0;

  void Push(ExecutionEvent event) {
    if (count < events.size()) events[count++] = std::move(event);
  }
};

class CommandExecutor {
 public:
  explicit CommandExecutor(LaserController *laser) : laser_(laser) {}

  ExecutionBatch Begin(const CommandEnvelope &command, uint64_t now_ms);
  ExecutionBatch Poll(uint64_t now_ms);

  bool command_pending() const { return pending_; }
  const std::string &pending_command_id() const { return pending_command_id_; }

 private:
  enum class Goal {
    kNone,
    kOn,
    kOff,
    kSafeOff,
    kFlashStarted,
    kFlashStopped,
  };

  ExecutionBatch Start(const CommandEnvelope &command, uint64_t now_ms);
  bool GoalReached(Goal goal) const;
  static bool IsSafetyPriority(const std::string &command_type);
  static bool IsRejected(ResultCode result);
  static ExecutionEvent Rejected(const std::string &command_id,
                                 ResultCode result);
  static ExecutionEvent Failed(const std::string &command_id,
                               ControllerFault fault);
  static ExecutionEvent Completed(const std::string &command_id,
                                  bool include_state = false);
  static ExecutionEvent Accepted(const std::string &command_id);

  LaserController *laser_ = nullptr;
  bool pending_ = false;
  std::string pending_command_id_;
  Goal pending_goal_ = Goal::kNone;
};

}  // namespace stagecore::stagelaser
