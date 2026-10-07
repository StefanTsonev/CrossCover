#pragma once

#include <cstddef>
#include <cstdint>

namespace library {

// Map a visible row to reading history, stored in newest-opened order.
inline uint16_t recentHistoryRow(const uint16_t row, const uint16_t total, const uint16_t* recentRows,
                                 const size_t recentCount, const bool descending) {
  if (row >= recentCount || recentCount > total || !recentRows) return UINT16_MAX;
  const uint16_t indexRow = recentRows[descending ? row : recentCount - 1 - row];
  return indexRow < total ? indexRow : UINT16_MAX;
}

}  // namespace library
