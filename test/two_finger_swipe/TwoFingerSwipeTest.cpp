#include <gtest/gtest.h>

#include "src/util/TwoFingerSwipe.h"

namespace {

TwoFingerSwipe::CompletedSwipe swipe(uint8_t contacts, int startX, int startY, int endX, int endY,
                                     unsigned long durationMs = 400) {
  return {contacts, startX, startY, endX, endY, durationMs};
}

}  // namespace

TEST(TwoFingerSwipe, RecognizesAllFourDirections) {
  EXPECT_EQ(TwoFingerSwipe::directionFor(swipe(2, 200, 700, 200, 400), 480, 800), TwoFingerSwipe::Direction::Up);
  EXPECT_EQ(TwoFingerSwipe::directionFor(swipe(2, 200, 100, 200, 400), 480, 800), TwoFingerSwipe::Direction::Down);
  EXPECT_EQ(TwoFingerSwipe::directionFor(swipe(2, 400, 200, 100, 200), 480, 800), TwoFingerSwipe::Direction::Left);
  EXPECT_EQ(TwoFingerSwipe::directionFor(swipe(2, 100, 200, 400, 200), 480, 800), TwoFingerSwipe::Direction::Right);
}

TEST(TwoFingerSwipe, LiveTranslationRejectsMovingRotation) {
  const TwoFingerSwipe::FingerPair start = {120, 120, 240, 120};
  const TwoFingerSwipe::FingerPair down = {120, 220, 240, 220};
  const TwoFingerSwipe::FingerPair rotatingDown = {140, 200, 220, 240};
  EXPECT_TRUE(TwoFingerSwipe::hasTranslationGeometry(start, down));
  EXPECT_TRUE(TwoFingerSwipe::fingersMovedTogether(start, down, TwoFingerSwipe::Direction::Down));
  const TwoFingerSwipe::FingerPair swappedDown = {240, 220, 120, 220};
  const auto aligned = TwoFingerSwipe::alignedPair(start, swappedDown);
  EXPECT_TRUE(TwoFingerSwipe::hasTranslationGeometry(start, aligned));
  EXPECT_TRUE(TwoFingerSwipe::fingersMovedTogether(start, aligned, TwoFingerSwipe::Direction::Down));
  EXPECT_FALSE(TwoFingerSwipe::hasTranslationGeometry(start, rotatingDown));
  EXPECT_TRUE(TwoFingerSwipe::fingersMovedTogether(start, rotatingDown, TwoFingerSwipe::Direction::Down));
}

TEST(TwoFingerSwipe, RejectsNonSwipeContactsAndAmbiguousMotion) {
  EXPECT_EQ(TwoFingerSwipe::directionFor(swipe(1, 200, 700, 200, 400), 480, 800), TwoFingerSwipe::Direction::None);
  EXPECT_EQ(TwoFingerSwipe::directionFor(swipe(2, 200, 700, 225, 680), 480, 800), TwoFingerSwipe::Direction::None);
  EXPECT_EQ(TwoFingerSwipe::directionFor(swipe(2, 200, 700, 400, 520), 480, 800), TwoFingerSwipe::Direction::None);
  EXPECT_EQ(TwoFingerSwipe::directionFor(swipe(2, 200, 700, 200, 400, 2001), 480, 800),
            TwoFingerSwipe::Direction::None);
}

TEST(TwoFingerSwipe, KeepsTheMostRecentlyAssignedAction) {
  uint8_t actions[] = {1, 2, 1, 0};
  EXPECT_TRUE(TwoFingerSwipe::clearDuplicateActions(actions, 0, 2));
  EXPECT_EQ(actions[0], 0);
  EXPECT_EQ(actions[1], 2);
  EXPECT_EQ(actions[2], 1);
  EXPECT_EQ(actions[3], 0);
}
