#pragma once

#include <cstdint>
#include <string>

namespace stagecore::stagelaser {

// Only actions that can enable (or logically re-enable) the output need a
// bounded Hub deadline. Emergency OFF and non-enabling actions are exempt.
inline bool RequiresFreshDeadline(const std::string &command_type,
                                  bool resync_to_on = false) {
  return command_type == "LASER_ARM" ||
         command_type == "LASER_SET_ON" ||
         command_type == "LASER_FLASH_START" ||
         (command_type == "LASER_STATE_RESYNC" && resync_to_on);
}

// Does NOT replace a persistent authenticated per-output generation. This
// only rejects an enabling frame that claims to have been issued implausibly
// far in the future relative to the trusted Hub-aligned device clock.
inline bool IssuedTooFarInFuture(int64_t issued_at_unix_ms,
                                 int64_t trusted_now_unix_ms,
                                 uint64_t max_future_skew_ms = 5000) {
  return trusted_now_unix_ms > 0 &&
         issued_at_unix_ms > trusted_now_unix_ms &&
         static_cast<uint64_t>(issued_at_unix_ms) -
                 static_cast<uint64_t>(trusted_now_unix_ms) >
             max_future_skew_ms;
}

}  // namespace stagecore::stagelaser
