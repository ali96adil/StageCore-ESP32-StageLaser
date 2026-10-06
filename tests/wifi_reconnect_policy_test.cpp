#include <cassert>
#include <cstdint>
#include <iostream>

#include "wifi_reconnect_policy.h"

int main() {
  using namespace stagecore::wifi_reconnect;

  assert(next_delay_ms(0) == kInitialDelayMs);
  assert(next_delay_ms(1000) == 2000);
  assert(next_delay_ms(2000) == 4000);
  assert(next_delay_ms(8000) == 15000);
  assert(next_delay_ms(15000) == 15000);
  assert(!recovery_portal_due(kRecoveryPortalDelayMs - 1));
  assert(recovery_portal_due(kRecoveryPortalDelayMs));

  std::cout << "StageLaser Wi-Fi reconnect policy PASS\n";
  return 0;
}
