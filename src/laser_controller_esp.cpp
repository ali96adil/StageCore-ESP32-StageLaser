#include "laser_controller_esp.h"

#include "laser_state_store.h"
#include "relay_output.h"

namespace stagecore::stagelaser {

bool NvsPersistentStateSink::Save(const PersistentState &state) {
  return save_persistent_state(state) == ESP_OK;
}

bool RelayOutputActuator::Pick() {
  return relay_pick() == ESP_OK;
}

bool RelayOutputActuator::Release() {
  return relay_release() == ESP_OK;
}

}  // namespace stagecore::stagelaser
