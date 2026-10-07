#include <gtest/gtest.h>

#include "util/ReleaseSuppression.h"

namespace {

TEST(ReleaseSuppressionTest, SuppressesOnlyTheMatchingBackReleaseFrame) {
  ReleaseSuppression suppression;
  suppression.suppressBack();

  suppression.expireAfterReleaseFrame({.backReleased = true});
  EXPECT_TRUE(suppression.consumeBackRelease());
  EXPECT_FALSE(suppression.consumeBackRelease());
}

TEST(ReleaseSuppressionTest, StaleBackSuppressionExpiresWithoutAConsumer) {
  ReleaseSuppression suppression;
  suppression.suppressBack();

  suppression.expireAfterReleaseFrame({.backReleased = true});
  suppression.expireAfterReleaseFrame({});

  EXPECT_FALSE(suppression.consumeBackRelease());
}

TEST(ReleaseSuppressionTest, ConfirmSuppressionCoversPhysicalConfirmAndPowerFallback) {
  ReleaseSuppression suppression;
  suppression.suppressConfirm();
  suppression.suppressPowerConfirm();

  suppression.expireAfterReleaseFrame({.powerReleased = true});
  EXPECT_TRUE(suppression.consumeConfirmRelease());
  EXPECT_FALSE(suppression.consumePowerConfirmRelease());
}

TEST(ReleaseSuppressionTest, StaleConfirmSuppressionExpiresAfterEitherSourceIsIdle) {
  ReleaseSuppression suppression;
  suppression.suppressConfirm();

  suppression.expireAfterReleaseFrame({.confirmReleased = true});
  suppression.expireAfterReleaseFrame({});

  EXPECT_FALSE(suppression.consumeConfirmRelease());
}

TEST(ReleaseSuppressionTest, PowerSuppressionRemainsDuringReleaseAndExpiresAfterward) {
  ReleaseSuppression suppression;
  suppression.suppressPower();

  suppression.expireAfterReleaseFrame({.powerReleased = true});
  EXPECT_TRUE(suppression.isPowerReleaseSuppressed());
  EXPECT_TRUE(suppression.consumePowerRelease());

  suppression.suppressPower();
  suppression.expireAfterReleaseFrame({});
  EXPECT_FALSE(suppression.isPowerReleaseSuppressed());
}

TEST(ReleaseSuppressionTest, PowerConfirmSuppressionExpiresWithoutAConsumer) {
  ReleaseSuppression suppression;
  suppression.suppressPowerConfirm();

  suppression.expireAfterReleaseFrame({.powerReleased = true});
  suppression.expireAfterReleaseFrame({});

  EXPECT_FALSE(suppression.consumePowerConfirmRelease());
}

TEST(ReleaseSuppressionTest, WakePowerHoldStaysSuppressedUntilReleasedThenAllowsTheNextPress) {
  ReleaseSuppression suppression;
  suppression.suppressPower();
  suppression.suppressPowerConfirm();

  for (int frame = 0; frame < 10; ++frame) {
    suppression.expireAfterReleaseFrame({.powerHeld = true});
    EXPECT_TRUE(suppression.isPowerReleaseSuppressed());
  }
  suppression.expireAfterReleaseFrame({.powerReleased = true});
  EXPECT_TRUE(suppression.isPowerReleaseSuppressed());
  suppression.expireAfterReleaseFrame({});
  EXPECT_FALSE(suppression.isPowerReleaseSuppressed());
  EXPECT_FALSE(suppression.consumePowerConfirmRelease());
  suppression.expireAfterReleaseFrame({.powerHeld = true});
  EXPECT_FALSE(suppression.isPowerReleaseSuppressed());
}

TEST(ReleaseSuppressionTest, NavigationHoldSwallowsOnlyItsOwnMenuOrBackRelease) {
  for (const bool fromMenu : {false, true}) {
    ReleaseSuppression suppression;
    if (fromMenu)
      suppression.suppressConfirm();
    else
      suppression.suppressBack();

    suppression.expireAfterReleaseFrame(fromMenu ? ReleaseSuppression::FrameState{.confirmHeld = true}
                                                 : ReleaseSuppression::FrameState{.backHeld = true});
    suppression.expireAfterReleaseFrame(fromMenu ? ReleaseSuppression::FrameState{.confirmReleased = true}
                                                 : ReleaseSuppression::FrameState{.backReleased = true});
    if (fromMenu) {
      EXPECT_TRUE(suppression.consumeConfirmRelease());
      EXPECT_FALSE(suppression.consumeConfirmRelease());
    } else {
      EXPECT_TRUE(suppression.consumeBackRelease());
      EXPECT_FALSE(suppression.consumeBackRelease());
    }
  }
}

}  // namespace
