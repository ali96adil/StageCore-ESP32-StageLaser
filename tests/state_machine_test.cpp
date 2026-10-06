#include <cassert>
#include <iostream>
#include "laser_state_machine.h"

using namespace stagecore::stagelaser;

static PersistentState tracked(LogicalState state) {
  PersistentState p;
  p.stable_state = state;
  p.quality = StateQuality::kTracked;
  return p;
}

static void release(StateMachine &m, uint64_t now) {
  auto d = m.Tick(now);
  assert(d.actuator == ActuatorAction::kRelease);
  m.ConfirmRelease(true, now);
}

int main() {
  {
    StateMachine m;
    m.Boot(tracked(LogicalState::kOff));
    assert(m.arm_state() == ArmState::kDisarmed);
    assert(m.CommandSetOn(0).result == ResultCode::kRejectedDisarmed);
    assert(m.CommandArm(0).result == ResultCode::kAccepted);
    auto on = m.CommandSetOn(1);
    assert(on.actuator == ActuatorAction::kPick);
    assert(m.logical_state() == LogicalState::kTurningOn);
    assert(m.relay_pulse_count() == 0);
    release(m, 181);
    assert(m.logical_state() == LogicalState::kOn);
    assert(m.relay_pulse_count() == 1);
    assert(m.CommandSetOn(500).result == ResultCode::kNoop);
    assert(m.relay_pulse_count() == 1);
    auto off = m.CommandSetOff(600);
    assert(off.actuator == ActuatorAction::kPick);
    release(m, 780);
    assert(m.logical_state() == LogicalState::kOff);
    assert(m.relay_pulse_count() == 2);
    assert(m.CommandSetOff(1100).result == ResultCode::kNoop);
  }

  {
    PersistentState interrupted = tracked(LogicalState::kOff);
    interrupted.interrupted_transition = true;
    StateMachine m;
    m.Boot(interrupted);
    assert(m.logical_state() == LogicalState::kUnknown);
    assert(m.CommandArm(0).result == ResultCode::kRejectedUnknown);
    assert(m.CommandSafeOff(0).result == ResultCode::kRejectedUnsafe);
    assert(m.relay_pulse_count() == 0);
    assert(m.CommandResyncOff().result == ResultCode::kAccepted);
    assert(m.logical_state() == LogicalState::kOff);
  }

  {
    StateMachine m;
    m.Boot(tracked(LogicalState::kOff));
    assert(m.CommandArm(0).result == ResultCode::kAccepted);
    auto start = m.CommandFlashStart(1000, FlashRequest{1.0, 2000});
    assert(start.actuator == ActuatorAction::kPick);
    release(m, 1180);
    assert(m.logical_state() == LogicalState::kFlashOn);
    auto edge = m.Tick(1680);
    assert(edge.actuator == ActuatorAction::kPick);
    release(m, 1860);
    assert(m.logical_state() == LogicalState::kFlashOff);
    m.CommandFlashStop(2200);
    auto settle = m.Tick(2300);
    (void)settle;
    assert(!m.flash_active());
    assert(m.PersistentSnapshot().stable_state == LogicalState::kOff);
  }

  {
    StateMachine m;
    m.Boot(tracked(LogicalState::kOn));
    auto safe = m.CommandSafeOff(0);
    assert(safe.actuator == ActuatorAction::kPick);
    release(m, 180);
    assert(m.arm_state() == ArmState::kDisarmed);
    assert(m.logical_state() == LogicalState::kOff);
  }

  {
    StateMachine m;
    m.Boot(tracked(LogicalState::kOff));
    assert(m.CommandArm(0).result == ResultCode::kAccepted);
    auto bad = m.CommandFlashStart(0, FlashRequest{2.0, 8000});
    assert(bad.result == ResultCode::kRejectedLimits);
    assert(m.relay_pulse_count() == 0);
  }

  std::cout << "StageLaser state-machine tests PASS\n";
  return 0;
}
