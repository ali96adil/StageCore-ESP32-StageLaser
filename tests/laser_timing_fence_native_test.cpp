#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

#include "laser_timing_fence.h"

int main() {
  using stagecore::stagelaser::IssuedTooFarInFuture;
  using stagecore::stagelaser::RequiresFreshDeadline;

  assert(RequiresFreshDeadline("LASER_ARM"));
  assert(RequiresFreshDeadline("LASER_SET_ON"));
  assert(RequiresFreshDeadline("LASER_FLASH_START"));
  assert(RequiresFreshDeadline("LASER_STATE_RESYNC", true));
  assert(!RequiresFreshDeadline("LASER_STATE_RESYNC", false));
  assert(!RequiresFreshDeadline("LASER_SET_OFF"));
  assert(!RequiresFreshDeadline("LASER_SAFE_OFF"));
  assert(!RequiresFreshDeadline("LASER_DISARM"));
  assert(!RequiresFreshDeadline("LASER_FLASH_STOP"));
  assert(!RequiresFreshDeadline("LASER_STATE_READ"));

  constexpr int64_t now = 1700000000000LL;
  assert(!IssuedTooFarInFuture(now, now));
  assert(!IssuedTooFarInFuture(now + 5000, now));
  assert(IssuedTooFarInFuture(now + 5001, now));
  assert(!IssuedTooFarInFuture(now - 3600000, now));
  assert(!IssuedTooFarInFuture(now + 100000, 0));  // untrusted clock handled upstream
  assert(IssuedTooFarInFuture(std::numeric_limits<int64_t>::max(), now));
  assert(!IssuedTooFarInFuture(now, std::numeric_limits<int64_t>::max()));
}
