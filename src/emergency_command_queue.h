#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace stagecore {

// Pure, GPIO-free queue policy. Callers must hold their own mutex and must
// validate the full authenticated command envelope before any actuation.
// Emergency hints can only reject older work; they grant no output authority.
class EmergencyCommandQueue {
 public:
  bool Push(std::string text, bool emergency) {
    if (text.empty()) return false;
    // Reserve one emergency slot beyond the eight ordinary command slots.
    // If even the emergency slot is full, the transport must fail closed.
    if (pending_.size() >= (emergency ? 9U : 8U)) return false;
    if (emergency) ++epoch_;
    Entry entry{std::move(text), epoch_};
    if (emergency) {
      pending_.insert(pending_.begin(), std::move(entry));
    } else {
      pending_.push_back(std::move(entry));
    }
    return true;
  }

  bool Pop(std::string *text, bool *superseded) {
    if (text == nullptr || superseded == nullptr || pending_.empty())
      return false;
    *superseded = pending_.front().epoch < epoch_;
    *text = std::move(pending_.front().text);
    pending_.erase(pending_.begin());
    return true;
  }

  bool empty() const { return pending_.empty(); }
  size_t size() const { return pending_.size(); }

 private:
  struct Entry {
    std::string text;
    uint64_t epoch;
  };
  std::vector<Entry> pending_;
  uint64_t epoch_ = 0;
};

}  // namespace stagecore
