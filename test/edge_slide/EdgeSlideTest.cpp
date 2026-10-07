#include <gtest/gtest.h>

#include "src/util/EdgeSlide.h"
#include "src/util/SwipeAdjustment.h"

TEST(EdgeSlide, ScalesLightAdjustmentWithSwipeLength) {
  EXPECT_EQ(SwipeAdjustment::amount(59, 800), 0);
  EXPECT_EQ(SwipeAdjustment::amount(60, 800), 1);
  EXPECT_EQ(SwipeAdjustment::amount(68, 800), 2);
  EXPECT_GT(SwipeAdjustment::amount(400, 800), SwipeAdjustment::amount(100, 800));
  EXPECT_EQ(SwipeAdjustment::amount(799, 800), 100);
  EXPECT_EQ(SwipeAdjustment::amount(1200, 800), 100);
  EXPECT_EQ(SwipeAdjustment::amount(60, 480), 1);
  EXPECT_EQ(SwipeAdjustment::amount(65, 480), 2);
  EXPECT_EQ(SwipeAdjustment::amount(479, 480), 100);
  for (const int axisSize : {480, 800}) {
    for (int distance = 61; distance < axisSize; ++distance) {
      EXPECT_LE(SwipeAdjustment::amount(distance, axisSize) - SwipeAdjustment::amount(distance - 1, axisSize), 1);
    }
  }
}

TEST(EdgeSlide, LiveLightTargetFollowsFingerBackFromLimit) {
  EXPECT_EQ(SwipeAdjustment::targetValue(95, true, 20), 100);
  EXPECT_EQ(SwipeAdjustment::targetValue(95, true, 5), 100);
  EXPECT_EQ(SwipeAdjustment::targetValue(95, true, 0), 95);
  EXPECT_EQ(SwipeAdjustment::targetValue(10, false, 20), 0);
  EXPECT_EQ(SwipeAdjustment::targetValue(10, false, 5), 5);
}

TEST(EdgeSlide, RecognizesBothSidesAndDirections) {
  EXPECT_EQ(EdgeSlide::directionFor(10, 500, 18, 350, 480, 800), EdgeSlide::Direction::LeftUp);
  EXPECT_EQ(EdgeSlide::directionFor(10, 350, 18, 500, 480, 800), EdgeSlide::Direction::LeftDown);
  EXPECT_EQ(EdgeSlide::directionFor(470, 500, 465, 350, 480, 800), EdgeSlide::Direction::RightUp);
  EXPECT_EQ(EdgeSlide::directionFor(470, 350, 465, 500, 480, 800), EdgeSlide::Direction::RightDown);
}

TEST(EdgeSlide, LeavesCenterAndDiagonalGesturesAlone) {
  EXPECT_EQ(EdgeSlide::directionFor(100, 500, 100, 350, 480, 800), EdgeSlide::Direction::None);
  EXPECT_EQ(EdgeSlide::directionFor(10, 500, 80, 350, 480, 800), EdgeSlide::Direction::None);
  EXPECT_EQ(EdgeSlide::directionFor(10, 500, 30, 460, 480, 800), EdgeSlide::Direction::None);
  EXPECT_EQ(EdgeSlide::directionFor(10, 500, 35, 450, 480, 800), EdgeSlide::Direction::None);
  EXPECT_EQ(EdgeSlide::directionFor(10, 400, 60, 340, 800, 480), EdgeSlide::Direction::None);
  EXPECT_EQ(EdgeSlide::directionFor(790, 500, 720, 350, 800, 800), EdgeSlide::Direction::None);
}
