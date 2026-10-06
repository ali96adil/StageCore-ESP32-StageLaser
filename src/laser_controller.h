#pragma once

#include <cstdint>

#include "laser_state_machine.h"

namespace stagecore::stagelaser {

enum class ControllerFault {
  kNone,
  kPersistence,
  kActuatorPick,
  kActuatorRelease,
};

struct ControllerOutcome {
  Decision decision{};
  ControllerFault fault = ControllerFault::kNone;
};

class PersistentStateSink {
 public:
  virtual ~PersistentStateSink() = default;
  virtual bool Save(const PersistentState &state) = 0;
};

class RelayActuator {
 public:
  virtual ~RelayActuator() = default;
  virtual bool Pick() = 0;
  virtual bool Release() = 0;
};

class LaserController {
 public:
  LaserController(const PersistentState &persisted, ResetClass reset,
                  bool shared_power_qualified, PersistentStateSink *store,
                  RelayActuator *actuator, Limits limits = {});

  ControllerOutcome Arm(uint64_t now_ms);
  ControllerOutcome Disarm(uint64_t now_ms);
  ControllerOutcome SetOn(uint64_t now_ms);
  ControllerOutcome SetOff(uint64_t now_ms);
  ControllerOutcome FlashStart(uint64_t now_ms, FlashRequest request);
  ControllerOutcome FlashStop(uint64_t now_ms);
  ControllerOutcome SafeOff(uint64_t now_ms);
  ControllerOutcome ResyncOff();
  ControllerOutcome ResyncOn();
  ControllerOutcome Poll(uint64_t now_ms);

  const StateMachine &machine() const { return machine_; }

 private:
  ControllerOutcome FinishMutation(const PersistentState &before,
                                   Decision decision);
  ControllerOutcome ApplyActuator(Decision decision,
                                  const PersistentState &before);
  void EnterUnknown();
  bool PersistCurrent();
  static bool SamePersistent(const PersistentState &a,
                             const PersistentState &b);

  StateMachine machine_;
  PersistentStateSink *store_ = nullptr;
  RelayActuator *actuator_ = nullptr;
};

}  // namespace stagecore::stagelaser
