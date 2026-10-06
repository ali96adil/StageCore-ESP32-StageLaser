#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "laser_controller.h"

using namespace stagecore::stagelaser;

namespace {

PersistentState tracked(LogicalState state) {
  PersistentState p;
  p.stable_state = state;
  p.quality = StateQuality::kTracked;
  return p;
}

struct FakeStore final : PersistentStateSink {
  bool allow = true;
  int writes = 0;
  PersistentState last{};
  std::vector<std::string> *events = nullptr;

  bool Save(const PersistentState &state) override {
    ++writes;
    last = state;
    if (events != nullptr) events->push_back("save");
    return allow;
  }
};

struct FakeRelay final : RelayActuator {
  bool pick_ok = true;
  bool release_ok = true;
  int picks = 0;
  int releases = 0;
  std::vector<std::string> *events = nullptr;

  bool Pick() override {
    ++picks;
    if (events != nullptr) events->push_back("pick");
    return pick_ok;
  }

  bool Release() override {
    ++releases;
    if (events != nullptr) events->push_back("release");
    return release_ok;
  }
};

}  // namespace

int main() {
  {
    std::vector<std::string> events;
    FakeStore store;
    FakeRelay relay;
    store.events = &events;
    relay.events = &events;

    LaserController c(tracked(LogicalState::kOff),
                      ResetClass::kSoftware, false, &store, &relay);
    assert(c.Arm(0).fault == ControllerFault::kNone);
    auto start = c.SetOn(1);
    assert(start.fault == ControllerFault::kNone);
    assert(start.decision.actuator == ActuatorAction::kPick);
    assert(events.size() == 2);
    assert(events[0] == "save");
    assert(events[1] == "pick");
    assert(c.machine().logical_state() == LogicalState::kTurningOn);
    assert(c.machine().relay_pulse_count() == 0);

    auto released = c.Poll(181);
    assert(released.decision.actuator == ActuatorAction::kRelease);
    assert(released.fault == ControllerFault::kNone);
    assert(c.machine().logical_state() == LogicalState::kOn);
    assert(c.machine().relay_pulse_count() == 1);
    assert(events.size() == 4);
    assert(events[2] == "release");
    assert(events[3] == "save");
  }

  {
    FakeStore store;
    FakeRelay relay;
    store.allow = false;
    LaserController c(tracked(LogicalState::kOff),
                      ResetClass::kSoftware, false, &store, &relay);
    assert(c.Arm(0).fault == ControllerFault::kNone);
    auto out = c.SetOn(1);
    assert(out.fault == ControllerFault::kPersistence);
    assert(relay.picks == 0);
    assert(c.machine().arm_state() == ArmState::kDisarmed);
    assert(c.machine().logical_state() == LogicalState::kOff);
  }

  {
    FakeStore store;
    FakeRelay relay;
    relay.pick_ok = false;
    LaserController c(tracked(LogicalState::kOff),
                      ResetClass::kSoftware, false, &store, &relay);
    c.Arm(0);
    auto out = c.SetOn(1);
    assert(out.fault == ControllerFault::kActuatorPick);
    assert(relay.picks == 1);
    assert(relay.releases == 1);
    assert(c.machine().arm_state() == ArmState::kDisarmed);
    assert(c.machine().logical_state() == LogicalState::kUnknown);
  }

  {
    FakeStore store;
    FakeRelay relay;
    relay.release_ok = false;
    LaserController c(tracked(LogicalState::kOff),
                      ResetClass::kSoftware, false, &store, &relay);
    c.Arm(0);
    assert(c.SetOn(1).fault == ControllerFault::kNone);
    auto out = c.Poll(181);
    assert(out.fault == ControllerFault::kActuatorRelease);
    assert(c.machine().logical_state() == LogicalState::kUnknown);
  }

  {
    FakeStore store;
    FakeRelay relay;
    LaserController c(tracked(LogicalState::kOff),
                      ResetClass::kSoftware, false, &store, &relay);
    c.Arm(0);

    auto start = c.FlashStart(1000, FlashRequest{1.0, 3000});
    assert(start.fault == ControllerFault::kNone);
    assert(start.decision.actuator == ActuatorAction::kPick);
    assert(store.writes == 1);  // one durable Flash-session marker

    assert(c.Poll(1180).fault == ControllerFault::kNone);
    assert(c.machine().flash_active());
    assert(store.writes == 1);  // no per-phase NVS write

    auto edge = c.Poll(1680);
    assert(edge.decision.actuator == ActuatorAction::kPick);
    assert(store.writes == 1);
    assert(c.Poll(1860).fault == ControllerFault::kNone);
    assert(store.writes == 1);

    auto stop = c.FlashStop(2200);
    assert(stop.fault == ControllerFault::kNone);
    assert(!c.machine().flash_active());
    assert(c.machine().logical_state() == LogicalState::kOff);
    assert(store.writes == 2);  // clear marker only at stable final OFF
  }

  {
    FakeStore store;
    FakeRelay relay;
    LaserController c(PersistentState{}, ResetClass::kSoftware,
                      false, &store, &relay);
    assert(c.machine().logical_state() == LogicalState::kUnknown);
    auto resync = c.ResyncOff();
    assert(resync.fault == ControllerFault::kNone);
    assert(c.machine().logical_state() == LogicalState::kOff);
    assert(store.writes == 1);
  }

  std::cout << "StageLaser controller transaction tests PASS\n";
  return 0;
}
