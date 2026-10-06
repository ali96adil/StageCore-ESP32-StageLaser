#include <cassert>
#include <cstdint>
#include <limits>

#include "recovery_policy.h"

int main() {
  using stagecore::LocalRecoveryHoldPolicy;

  // 2-second action fires once per continuous hold.
  LocalRecoveryHoldPolicy emergency(2000);
  for (int i = 0; i < 39; ++i) {
    assert(!emergency.sample(true, 50));
  }
  assert(emergency.held_ms() == 1950);
  assert(emergency.sample(true, 50));
  assert(emergency.held_ms() == 2000);
  assert(!emergency.sample(true, 5000));

  // Releasing resets both accumulated time and the one-shot trigger.
  assert(!emergency.sample(false, 50));
  assert(emergency.held_ms() == 0);
  for (int i = 0; i < 39; ++i) {
    assert(!emergency.sample(true, 50));
  }
  assert(emergency.sample(true, 50));

  // 10-second trust-reset threshold stays independent.
  LocalRecoveryHoldPolicy trust_reset(10000);
  for (int i = 0; i < 199; ++i) {
    assert(!trust_reset.sample(true, 50));
  }
  assert(trust_reset.sample(true, 50));
  assert(!trust_reset.sample(true, 50));

  // Saturating accumulation must never wrap and accidentally delay a trigger.
  LocalRecoveryHoldPolicy overflow(std::numeric_limits<uint32_t>::max());
  assert(!overflow.sample(true, std::numeric_limits<uint32_t>::max() - 10));
  assert(overflow.sample(true, 100));
  assert(overflow.held_ms() == std::numeric_limits<uint32_t>::max());

  return 0;
}
