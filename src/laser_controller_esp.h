#pragma once

#include "laser_controller.h"

namespace stagecore::stagelaser {

class NvsPersistentStateSink final : public PersistentStateSink {
 public:
  bool Save(const PersistentState &state) override;
};

class RelayOutputActuator final : public RelayActuator {
 public:
  bool Pick() override;
  bool Release() override;
};

}  // namespace stagecore::stagelaser
