#pragma once

#include <cstdint>

struct CrossPointSettings {
  uint8_t trackReadingStats = 1;
  bool shouldTrackReadingStats() const { return trackReadingStats != 0; }
};

inline CrossPointSettings SETTINGS;
