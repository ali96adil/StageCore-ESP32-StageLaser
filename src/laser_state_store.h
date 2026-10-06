#pragma once

#include "esp_err.h"
#include "laser_contract.h"

namespace stagecore::stagelaser {

esp_err_t load_persistent_state(PersistentState *state, bool *found);
esp_err_t save_persistent_state(const PersistentState &state);
esp_err_t clear_persistent_state();

}  // namespace stagecore::stagelaser
