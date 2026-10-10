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
    m.Boot(tracked(LogicalState::kOff), ResetClass::kSoftware, false);
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
    m.Boot(interrupted, ResetClass::kSoftware, false);
    assert(m.logical_state() == LogicalState::kUnknown);
    assert(m.CommandArm(0).result == ResultCode::kRejectedUnknown);
    assert(m.CommandSafeOff(0).result == ResultCode::kRejectedUnsafe);
    assert(m.relay_pulse_count() == 0);
    assert(m.CommandResyncOff().result == ResultCode::kAccepted);
    assert(m.logical_state() == LogicalState::kOff);
  }

  {
    StateMachine m;
    m.Boot(tracked(LogicalState::kOff), ResetClass::kSoftware, false);
    assert(m.CommandArm(0).result == ResultCode::kAccepted);
    auto start = m.CommandFlashStart(1000, FlashRequest{1.0, 2000});
    assert(start.actuator == ActuatorAction::kPick);
    release(m, 1180);
    assert(m.logical_state() == LogicalState::kFlashOn);

    const PersistentState during_flash = m.PersistentSnapshot();
    assert(during_flash.flash_session_in_progress);
    StateMachine rebooted;
    rebooted.Boot(during_flash, ResetClass::kSoftware, false);
    assert(rebooted.arm_state() == ArmState::kDisarmed);
    assert(rebooted.logical_state() == LogicalState::kUnknown);
    assert(rebooted.CommandSafeOff(0).result == ResultCode::kRejectedUnsafe);
    assert(rebooted.relay_pulse_count() == m.relay_pulse_count());

    auto edge = m.Tick(1680);
    assert(edge.actuator == ActuatorAction::kPick);
    release(m, 1860);
    assert(m.logical_state() == LogicalState::kFlashOff);
    m.CommandFlashStop(2200);
    auto settle = m.Tick(2300);
    (void)settle;
    assert(!m.flash_active());
    assert(!m.PersistentSnapshot().flash_session_in_progress);
    assert(m.PersistentSnapshot().stable_state == LogicalState::kOff);
  }

  {
    StateMachine m;
    m.Boot(tracked(LogicalState::kOn), ResetClass::kSoftware, false);
    auto safe = m.CommandSafeOff(0);
    assert(safe.actuator == ActuatorAction::kPick);
    release(m, 180);
    assert(m.arm_state() == ArmState::kDisarmed);
    assert(m.logical_state() == LogicalState::kOff);
  }

  {
    StateMachine m;
    m.Boot(tracked(LogicalState::kOff), ResetClass::kSoftware, false);
    assert(m.CommandArm(0).result == ResultCode::kAccepted);
    assert(m.CommandSetOn(1).actuator == ActuatorAction::kPick);
    release(m, 181);
    assert(m.logical_state() == LogicalState::kOn);

    auto disarm = m.CommandDisarm(200);
    assert(disarm.result == ResultCode::kAccepted);
    assert(disarm.actuator == ActuatorAction::kNone);
    assert(m.arm_state() == ArmState::kDisarmed);
    assert(m.safe_off_pending());
    assert(m.Tick(430).actuator == ActuatorAction::kNone);
    auto deferred_off = m.Tick(431);
    assert(deferred_off.actuator == ActuatorAction::kPick);
    release(m, 611);
    assert(m.logical_state() == LogicalState::kOff);
    assert(!m.safe_off_pending());
    assert(m.relay_pulse_count() == 2);
  }

  {
    StateMachine m;
    m.Boot(tracked(LogicalState::kOff), ResetClass::kSoftware, false);
    assert(m.CommandArm(0).result == ResultCode::kAccepted);
    assert(m.CommandSetOn(1).actuator == ActuatorAction::kPick);
    auto safe = m.CommandSafeOff(50);
    assert(safe.result == ResultCode::kAccepted);
    assert(m.safe_off_pending());
    release(m, 181);
    assert(m.logical_state() == LogicalState::kOn);
    assert(m.safe_off_pending());
    assert(m.Tick(431).actuator == ActuatorAction::kPick);
    release(m, 611);
    assert(m.logical_state() == LogicalState::kOff);
    assert(!m.safe_off_pending());
  }

  {
    // A safety OFF received while the flash's initial ON pulse is in
    // progress must finish the relay release and then drive OFF, never
    // resume flash or accept ON without a new explicit ARM.
    StateMachine m;
    m.Boot(tracked(LogicalState::kOff), ResetClass::kSoftware, false);
    assert(m.CommandArm(0).result == ResultCode::kAccepted);
    assert(m.CommandFlashStart(1000, FlashRequest{1.0, 3000}).actuator ==
           ActuatorAction::kPick);
    assert(m.CommandSafeOff(1050).result == ResultCode::kAccepted);
    assert(m.arm_state() == ArmState::kDisarmed);
    assert(m.safe_off_pending());
    release(m, 1180);
    assert(m.safe_off_pending());
    assert(m.Tick(1429).actuator == ActuatorAction::kNone);
    assert(m.Tick(1430).actuator == ActuatorAction::kPick);
    release(m, 1610);
    assert(m.logical_state() == LogicalState::kOff);
    assert(!m.flash_active());
    assert(!m.safe_off_pending());
    assert(m.CommandSetOn(1700).result == ResultCode::kRejectedDisarmed);
    assert(m.Tick(5000).actuator == ActuatorAction::kNone);
  }

  {
    // A SAFE_OFF after the flash has reached ON must cancel all later
    // flashing edges, including the edge that would otherwise start at
    // 1680ms. There is no implicit re-arm.
    StateMachine m;
    m.Boot(tracked(LogicalState::kOff), ResetClass::kSoftware, false);
    assert(m.CommandArm(0).result == ResultCode::kAccepted);
    assert(m.CommandFlashStart(1000, FlashRequest{1.0, 3000}).actuator ==
           ActuatorAction::kPick);
    release(m, 1180);
    assert(m.flash_active());
    assert(m.CommandSafeOff(1300).result == ResultCode::kAccepted);
    assert(m.Tick(1429).actuator == ActuatorAction::kNone);
    assert(m.Tick(1430).actuator == ActuatorAction::kPick);
    release(m, 1610);
    assert(m.logical_state() == LogicalState::kOff);
    assert(!m.flash_active());
    assert(m.Tick(1680).actuator == ActuatorAction::kNone);
    assert(m.Tick(5000).actuator == ActuatorAction::kNone);
    assert(m.CommandFlashStart(5001, FlashRequest{1.0, 1000}).result ==
           ResultCode::kRejectedDisarmed);
  }

  {
    StateMachine m;
    m.Boot(tracked(LogicalState::kOff), ResetClass::kSoftware, false);
    assert(m.CommandArm(0).result == ResultCode::kAccepted);
    auto bad = m.CommandFlashStart(0, FlashRequest{2.0, 8000});
    assert(bad.result == ResultCode::kRejectedLimits);
    assert(m.relay_pulse_count() == 0);
  }

  {
    const PersistentState prior_on = tracked(LogicalState::kOn);

    StateMachine software;
    software.Boot(prior_on, ResetClass::kSoftware, false);
    assert(software.arm_state() == ArmState::kDisarmed);
    assert(software.logical_state() == LogicalState::kOn);

    StateMachine watchdog;
    watchdog.Boot(prior_on, ResetClass::kWatchdog, false);
    assert(watchdog.logical_state() == LogicalState::kOn);

    StateMachine power_unqualified;
    power_unqualified.Boot(prior_on, ResetClass::kPowerOn, false);
    assert(power_unqualified.logical_state() == LogicalState::kUnknown);
    assert(power_unqualified.CommandSafeOff(0).result ==
           ResultCode::kRejectedUnsafe);

    StateMachine brownout_unqualified;
    brownout_unqualified.Boot(prior_on, ResetClass::kBrownout, false);
    assert(brownout_unqualified.logical_state() == LogicalState::kUnknown);

    StateMachine power_shared;
    power_shared.Boot(prior_on, ResetClass::kPowerOn, true);
    assert(power_shared.logical_state() == LogicalState::kOff);
    assert(power_shared.state_quality() == StateQuality::kTracked);
  }

  std::cout << "StageLaser state-machine tests PASS\n";
  return 0;
}
