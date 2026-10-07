#pragma once

#include "laser_contract.h"

namespace stagecore::stagelaser {

// Safety validation shared by persistent storage and host tests.
// Bounds mirror the StageCore V1 contract while keeping future electronic
// drivers possible without changing the public command vocabulary.
bool limits_valid(const Limits &limits);

}  // namespace stagecore::stagelaser
