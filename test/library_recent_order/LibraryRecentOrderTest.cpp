#include <LibraryRecentOrder.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace {
std::vector<uint16_t> shelf(const uint16_t total, const std::vector<uint16_t>& recents, const bool descending) {
  std::vector<uint16_t> out;
  for (uint16_t row = 0; row < recents.size(); ++row) {
    out.push_back(library::recentHistoryRow(row, total, recents.data(), recents.size(), descending));
  }
  return out;
}
}  // namespace

TEST(LibraryRecentOrder, OnlyOpenedBooksAppearInRecentOrder) {
  EXPECT_EQ(shelf(7, {2, 5, 0}, true), (std::vector<uint16_t>{2, 5, 0}));
  EXPECT_EQ(shelf(7, {2, 5, 0}, false), (std::vector<uint16_t>{0, 5, 2}));
}

TEST(LibraryRecentOrder, NoHistoryShowsNoBooks) {
  EXPECT_TRUE(shelf(4, {}, true).empty());
  EXPECT_TRUE(shelf(4, {}, false).empty());
}

TEST(LibraryRecentOrder, WholeShelfCanHaveReadingHistory) {
  EXPECT_EQ(shelf(3, {1, 2, 0}, true), (std::vector<uint16_t>{1, 2, 0}));
  EXPECT_EQ(shelf(3, {1, 2, 0}, false), (std::vector<uint16_t>{0, 2, 1}));
}

TEST(LibraryRecentOrder, EverySmallHistoryPermutationVisitsOnlyOpenedBooks) {
  std::vector<uint16_t> order{0, 1, 2, 3, 4};
  do {
    for (size_t size = 0; size <= order.size(); ++size) {
      const std::vector<uint16_t> recents(order.begin(), order.begin() + size);
      for (const bool descending : {false, true}) {
        auto rows = shelf(5, recents, descending);
        EXPECT_EQ(rows.size(), size);
        std::sort(rows.begin(), rows.end());
        auto expected = recents;
        std::sort(expected.begin(), expected.end());
        EXPECT_EQ(rows, expected);
      }
    }
  } while (std::next_permutation(order.begin(), order.end()));
}

TEST(LibraryRecentOrder, MaximumLibraryAndEighteenRecentBooks) {
  const std::vector<uint16_t> recents{4095, 4094, 4093, 4092, 4091, 4090, 4089, 4088, 4087,
                                      4086, 4085, 4084, 4083, 4082, 4081, 4080, 4079, 4078};
  auto rows = shelf(4096, recents, true);
  EXPECT_EQ(rows.size(), recents.size());
  EXPECT_EQ(rows.front(), 4095);
  EXPECT_EQ(rows.back(), 4078);
}

TEST(LibraryRecentOrder, EmptyAndOutOfRangeRowsAreRejected) {
  const uint16_t recents[]{2, 0};
  EXPECT_EQ(library::recentHistoryRow(0, 0, nullptr, 0, true), UINT16_MAX);
  EXPECT_EQ(library::recentHistoryRow(2, 5, recents, 2, true), UINT16_MAX);
  EXPECT_EQ(library::recentHistoryRow(0, 1, recents, 2, true), UINT16_MAX);
  EXPECT_EQ(library::recentHistoryRow(0, 2, recents, 2, true), UINT16_MAX);
}
