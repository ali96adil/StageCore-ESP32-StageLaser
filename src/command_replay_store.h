#pragma once

#include <cstddef>
#include <cstdint>
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

// Conservative, reboot-persistent timestamp barrier for emergency OFF.
// Returns 0 when no barrier has been stored. Corrupt/unavailable NVS is an
// error, never interpreted as an empty barrier.
esp_err_t command_emergency_watermark_load(int64_t *issued_at_unix_ms);

// Persist the maximum validated emergency OFF issued-at before processing
// that command. A lower timestamp never rewinds the durable barrier.
esp_err_t command_emergency_watermark_remember(int64_t issued_at_unix_ms);

}  // namespace stagecore::stagelaser
