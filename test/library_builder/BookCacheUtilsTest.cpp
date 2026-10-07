#include <gtest/gtest.h>

#include <string>

#include "../../src/activities/reader/BookStatsTracking.h"
#include "../../src/util/BookCacheUtils.h"
#include "CrossPointSettings.h"
#include "Epub.h"
#include "HalStorage.h"

namespace {

class BookCacheUtilsTest : public ::testing::Test {
 protected:
  std::string cachePath;

  void SetUp() override {
    fake::reset();
    SETTINGS.trackReadingStats = 1;
    cachePath = Epub("/a.epub", "/.crosspoint").getCachePath();
    fake::add(cachePath + "/progress.bin", "progress");
    fake::add(cachePath + "/stats_v5.bin", "stats");
    fake::add(cachePath + "/reader_settings.bin", "settings");
    fake::add(cachePath + "/reading_stats_off", "off");
    fake::add(cachePath + "/book.bin", "derived");
  }

  void expectUserStateIntact() {
    const auto expectFile = [](const std::string& path, const std::string& expected) {
      ASSERT_TRUE(Storage.exists(path.c_str())) << path;
      const auto& bytes = fake::files.at(path)->bytes;
      EXPECT_EQ(std::string(bytes.begin(), bytes.end()), expected);
    };
    expectFile(cachePath + "/progress.bin", "progress");
    expectFile(cachePath + "/stats_v5.bin", "stats");
    expectFile(cachePath + "/reader_settings.bin", "settings");
    expectFile(cachePath + "/reading_stats_off", "off");
  }
};

TEST_F(BookCacheUtilsTest, ClearsDerivedFilesWhileKeepingReadingState) {
  ASSERT_TRUE(clearBookCachePreservingUserState("/a.epub"));
  expectUserStateIntact();
  EXPECT_FALSE(Storage.exists((cachePath + "/book.bin").c_str()));
  EXPECT_EQ(fake::directoryEntriesByPath[cachePath], 0u);
}

TEST_F(BookCacheUtilsTest, RetryRecoversStatsAfterFailedRestore) {
  fake::failRenameToPath = cachePath + "/stats_v5.bin";
  ASSERT_FALSE(clearBookCachePreservingUserState("/a.epub"));
  ASSERT_TRUE(Storage.exists((cachePath + ".upload_preserve_stats_v5.bin").c_str()));
  ASSERT_TRUE(Storage.exists((cachePath + ".upload_preserve_stats_pending").c_str()));

  ASSERT_TRUE(clearBookCachePreservingUserState("/a.epub"));
  expectUserStateIntact();
  EXPECT_FALSE(Storage.exists((cachePath + ".upload_preserve_stats_v5.bin").c_str()));
  EXPECT_FALSE(Storage.exists((cachePath + ".upload_preserve_stats_pending").c_str()));
}

TEST_F(BookCacheUtilsTest, RetryRecoversProgressAfterFailedRestore) {
  fake::failRenameToPath = cachePath + "/progress.bin";
  ASSERT_FALSE(clearBookCachePreservingUserState("/a.epub"));
  ASSERT_TRUE(Storage.exists((cachePath + ".upload_preserve_progress.bin").c_str()));

  ASSERT_TRUE(clearBookCachePreservingUserState("/a.epub"));
  expectUserStateIntact();
  EXPECT_FALSE(Storage.exists((cachePath + ".upload_preserve_progress.bin").c_str()));
}

TEST_F(BookCacheUtilsTest, BookTrackingDefaultsOnAndGlobalOffTakesPrecedence) {
  fake::files.erase(cachePath + "/reading_stats_off");
  EXPECT_TRUE(BookStatsTracking::isEnabled(cachePath));
  SETTINGS.trackReadingStats = 0;
  EXPECT_FALSE(BookStatsTracking::isEnabled(cachePath));
  SETTINGS.trackReadingStats = 1;
  EXPECT_TRUE(BookStatsTracking::isEnabled(cachePath));
}

TEST_F(BookCacheUtilsTest, BookChoiceSurvivesCacheClearAndStatsDeletion) {
  ASSERT_FALSE(BookStatsTracking::isBookEnabled(cachePath));
  ASSERT_TRUE(clearBookCachePreservingUserState("/a.epub"));
  ASSERT_FALSE(BookStatsTracking::isBookEnabled(cachePath));
  ASSERT_TRUE(Storage.remove((cachePath + "/stats_v5.bin").c_str()));
  EXPECT_FALSE(BookStatsTracking::isBookEnabled(cachePath));
  ASSERT_TRUE(BookStatsTracking::setBookEnabled(cachePath, true));
  EXPECT_TRUE(BookStatsTracking::isEnabled(cachePath));
}

TEST_F(BookCacheUtilsTest, FailedBookToggleKeepsPreviousChoice) {
  ASSERT_TRUE(BookStatsTracking::setBookEnabled(cachePath, true));
  fake::failRenameToPath = cachePath + "/reading_stats_off";
  EXPECT_FALSE(BookStatsTracking::setBookEnabled(cachePath, false));
  EXPECT_TRUE(BookStatsTracking::isBookEnabled(cachePath));
  EXPECT_FALSE(Storage.exists((cachePath + "/reading_stats_off.tmp").c_str()));
  ASSERT_TRUE(BookStatsTracking::setBookEnabled(cachePath, false));
  EXPECT_FALSE(BookStatsTracking::isEnabled(cachePath));
}

}  // namespace
