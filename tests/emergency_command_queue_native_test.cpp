#include <cassert>
#include <string>
#include "emergency_command_queue.h"

int main() {
  using stagecore::EmergencyCommandQueue;
  {
    EmergencyCommandQueue q;
    assert(q.Push("ON-1", false));
    assert(q.Push("ON-2", false));
    std::string text;
    bool stale = true;
    assert(q.Pop(&text, &stale) && text == "ON-1" && !stale);
    assert(q.Pop(&text, &stale) && text == "ON-2" && !stale);
    assert(!q.Pop(&text, &stale));
  }
  {
    EmergencyCommandQueue q;
    for (int i = 0; i < 8; ++i) {
      assert(q.Push("ON-" + std::to_string(i), false));
    }
    assert(!q.Push("ON-9", false));
    assert(q.Push("SAFE_OFF", true));
    assert(q.size() == 9);
    assert(!q.Push("ARM", false));
    std::string text;
    bool stale = false;
    assert(q.Pop(&text, &stale) && text == "SAFE_OFF" && !stale);
    for (int i = 0; i < 8; ++i) {
      assert(q.Pop(&text, &stale));
      assert(text == "ON-" + std::to_string(i));
      assert(stale);
    }
    assert(q.empty());
  }
  {
    EmergencyCommandQueue q;
    assert(q.Push("ON-1", false));
    assert(q.Push("SAFE_OFF-1", true));
    assert(q.Push("ON-2", false));
    assert(q.Push("SAFE_OFF-2", true));
    std::string text;
    bool stale = false;
    assert(q.Pop(&text, &stale) && text == "SAFE_OFF-2" && !stale);
    assert(q.Pop(&text, &stale) && text == "SAFE_OFF-1" && stale);
    assert(q.Pop(&text, &stale) && text == "ON-1" && stale);
    assert(q.Pop(&text, &stale) && text == "ON-2" && stale);
    // A command received AFTER the barrier is not automatically stale:
    // authenticated Hub/device generation fencing is a separate release gate.
    assert(q.Push("NEW-ON", false));
    assert(q.Pop(&text, &stale) && text == "NEW-ON" && !stale);
  }
  {
    EmergencyCommandQueue q;
    for (int i = 0; i < 8; ++i) assert(q.Push("ON", false));
    assert(q.Push("SAFE_OFF-1", true));
    // No silent dropping of a second emergency when the 9-slot queue is full.
    // The transport must fail closed rather than claiming it was delivered.
    assert(!q.Push("SAFE_OFF-2", true));
  }
}
