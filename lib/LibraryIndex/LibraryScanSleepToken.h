#pragma once

#include <cstdint>

namespace library {
// Kept in RTC memory by main.cpp. No constructor: uninitialized RTC storage
// must be validated before use and consumed even on a reset or cold boot.
struct ScanSleepToken {
  uint32_t magic;
  uint32_t check;
  static constexpr uint32_t MAGIC = 0x4C494233;

  void save(const bool current) {
    magic = current ? MAGIC : 0;
    check = current ? ~MAGIC : 0;
  }
  bool consume(const bool deepSleepWake) {
    const bool current = deepSleepWake && magic == MAGIC && check == ~MAGIC;
    magic = check = 0;
    return current;
  }
};
}  // namespace library
