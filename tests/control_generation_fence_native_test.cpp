#include <cassert>
#include <cstdint>
#include "control_generation_fence.h"

int main() {
  using stagecore::stagelaser::ControlGenerationDecision;
  using stagecore::stagelaser::DecideControlGeneration;
  using D = ControlGenerationDecision;

  assert(DecideControlGeneration(1, 0, false) == D::kAdvance);
  assert(DecideControlGeneration(2, 1, true) == D::kAdvance);
  assert(DecideControlGeneration(1, 2, false) == D::kReject);
  assert(DecideControlGeneration(2, 2, false) == D::kReject);
  assert(DecideControlGeneration(1, 2, true) == D::kSafeOnly);
  assert(DecideControlGeneration(2, 2, true) == D::kSafeOnly);
  assert(DecideControlGeneration(3, 2, false) == D::kAdvance);
  // Device restart must restore the maximum from NVS: never rewind it.
  assert(DecideControlGeneration(2, 3, false) == D::kReject);
  assert(DecideControlGeneration(4, 3, false) == D::kAdvance);
  // Invalid or non-exact JSON integer bounds always fail closed.
  constexpr uint64_t kMax = 9007199254740991ULL;
  assert(DecideControlGeneration(0, 0, true) == D::kReject);
  assert(DecideControlGeneration(kMax + 1, 0, true) == D::kReject);
  assert(DecideControlGeneration(1, kMax + 1, true) == D::kReject);
  assert(DecideControlGeneration(kMax, kMax - 1, false) == D::kAdvance);
  assert(DecideControlGeneration(kMax, kMax, false) == D::kReject);
}
