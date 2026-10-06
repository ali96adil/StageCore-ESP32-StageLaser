#pragma once

#include <cstddef>
#include <string>

#include "esp_err.h"

namespace stagecore::stagelaser {

constexpr size_t kPersistentCommandReplayCapacity = 32;

// Returns whether this exact command_id has already crossed the durable
// at-most-once fence. Corrupt or unavailable storage is an error, never a miss.
esp_err_t command_replay_seen(const std::string &command_id, bool *seen);

// Atomically remembers command_id before any command is allowed to actuate.
// Re-remembering an existing ID is a no-op.
esp_err_t command_replay_remember(const std::string &command_id);

}  // namespace stagecore::stagelaser
