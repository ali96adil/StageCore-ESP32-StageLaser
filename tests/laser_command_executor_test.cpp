#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "laser_command_executor.h"

using namespace stagecore::stagelaser;

namespace {

PersistentState tracked(LogicalState state) {
  PersistentState p;
  p.stable_state = state;
  p.quality = StateQuality::kTracked;
  return p;
}

struct FakeStore final : PersistentStateSink {
  bool Save(const PersistentState &) override { return true; }
};

struct FakeRelay final : RelayActuator {
  int picks = 0;
  int releases = 0;
  bool Pick() override { ++picks; return true; }
  bool Release() override { ++releases; return true; }
};

CommandEnvelope command(const char *id, const char *type) {
  CommandEnvelope c;
  c.command_id = id;
  c.command_type = type;
  return c;
}

}  // namespace

int main() {
  {
    FakeStore store;
    FakeRelay relay;
    LaserController laser(tracked(LogicalState::kOff),
                          ResetClass::kSoftware, false, &store, &relay);
    CommandExecutor exec(&laser);

    auto arm = exec.Begin(command("arm-1", "LASER_ARM"), 0);
    assert(arm.count == 1);
    assert(arm.events[0].kind == ExecutionEventKind::kCompleted);

    auto on = exec.Begin(command("on-1", "LASER_SET_ON"), 1);
    assert(on.count == 1);
    assert(on.events[0].kind == ExecutionEventKind::kAccepted);
    assert(exec.command_pending());
    assert(relay.picks == 1);

    auto mid = exec.Poll(100);
    assert(mid.count == 0);
    auto done = exec.Poll(181);
    assert(done.count == 1);
    assert(done.events[0].kind == ExecutionEventKind::kCompleted);
    assert(done.events[0].command_id == "on-1");
    assert(!exec.command_pending());
    assert(relay.releases == 1);
  }

  {
    FakeStore store;
    FakeRelay relay;
    LaserController laser(tracked(LogicalState::kOff),
                          ResetClass::kSoftware, false, &store, &relay);
    CommandExecutor exec(&laser);
    exec.Begin(command("arm-2", "LASER_ARM"), 0);
    auto pending = exec.Begin(command("on-2", "LASER_SET_ON"), 1);
    assert(pending.events[0].kind == ExecutionEventKind::kAccepted);

    auto safe = exec.Begin(command("safe-2", "LASER_SAFE_OFF"), 50);
    assert(safe.count == 2);
    assert(safe.events[0].kind == ExecutionEventKind::kFailed);
    assert(safe.events[0].command_id == "on-2");
    assert(safe.events[0].error_code == "LASER_COMMAND_PREEMPTED");
    assert(safe.events[1].kind == ExecutionEventKind::kAccepted);
    assert(safe.events[1].command_id == "safe-2");

    assert(exec.Poll(181).count == 0);
    assert(exec.Poll(430).count == 0);
    assert(exec.Poll(431).count == 0);
    auto off = exec.Poll(611);
    assert(off.count == 1);
    assert(off.events[0].kind == ExecutionEventKind::kCompleted);
    assert(off.events[0].command_id == "safe-2");
    assert(laser.machine().logical_state() == LogicalState::kOff);
    assert(laser.machine().arm_state() == ArmState::kDisarmed);
    assert(relay.picks == 2);
  }

  {
    FakeStore store;
    FakeRelay relay;
    LaserController laser(tracked(LogicalState::kOff),
                          ResetClass::kSoftware, false, &store, &relay);
    CommandExecutor exec(&laser);
    exec.Begin(command("arm-3", "LASER_ARM"), 0);

    CommandEnvelope flash = command("flash-3", "LASER_FLASH_START");
    flash.flash_frequency_hz = 1.0;
    flash.flash_duration_ms = 2000;
    auto start = exec.Begin(flash, 1000);
    assert(start.events[0].kind == ExecutionEventKind::kAccepted);
    auto started = exec.Poll(1180);
    assert(started.count == 1);
    assert(started.events[0].kind == ExecutionEventKind::kCompleted);
    assert(!exec.command_pending());
    assert(laser.machine().flash_active());

    assert(exec.Poll(1680).count == 0);
    assert(relay.picks == 2);
    assert(exec.Poll(1860).count == 0);
    assert(relay.releases == 2);
  }

  {
    FakeStore store;
    FakeRelay relay;
    LaserController laser(PersistentState{}, ResetClass::kSoftware,
                          false, &store, &relay);
    CommandExecutor exec(&laser);

    CommandEnvelope resync = command("resync-4", "LASER_STATE_RESYNC");
    resync.resync_state = "OFF";
    auto out = exec.Begin(resync, 0);
    assert(out.count == 1);
    assert(out.events[0].kind == ExecutionEventKind::kCompleted);
    assert(out.events[0].include_state_payload);
    assert(laser.machine().logical_state() == LogicalState::kOff);

    auto state = exec.Begin(command("read-4", "LASER_STATE_READ"), 1);
    assert(state.count == 1);
    assert(state.events[0].kind == ExecutionEventKind::kCompleted);
    assert(state.events[0].include_state_payload);
  }

  std::cout << "StageLaser command executor tests PASS\n";
  return 0;
}
