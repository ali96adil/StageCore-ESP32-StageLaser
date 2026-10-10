#pragma once

#include <cstdint>

namespace stagecore::stagelaser {

// StageLaser controls one logical output. This is a receive-side, durable
// generation fence. Authority also requires the authenticated Hub socket,
// exact Project/Runtime Snapshot, command deadline and physical interlocks.
enum class ControlGenerationDecision {
  kReject,
  kSafeOnly,
  kAdvance,
};

inline ControlGenerationDecision DecideControlGeneration(
    uint64_t incoming, uint64_t durable_max, bool emergency_off) {
  constexpr uint64_t kMaximumExactGeneration = 9007199254740991ULL;
  if (incoming == 0 || incoming > kMaximumExactGeneration ||
      durable_max > kMaximumExactGeneration)
    return ControlGenerationDecision::kReject;
  if (incoming <= durable_max)
    return emergency_off ? ControlGenerationDecision::kSafeOnly
                         : ControlGenerationDecision::kReject;
  return ControlGenerationDecision::kAdvance;
}

}  // namespace stagecore::stagelaser
