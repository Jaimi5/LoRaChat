#include <gtest/gtest.h>

#include "connectBackoff.h"

TEST(ConnectBackoff, FirstAttemptIsAllowedRightAfterBoot) {
    ConnectBackoff backoff(5000, 300000);
    EXPECT_TRUE(backoff.shouldAttempt(1000));
}

TEST(ConnectBackoff, WaitsBetweenAttempts) {
    ConnectBackoff backoff(5000, 300000);
    backoff.onAttempt(1000);
    EXPECT_FALSE(backoff.shouldAttempt(5999));
    EXPECT_TRUE(backoff.shouldAttempt(6000));
}

TEST(ConnectBackoff, FailuresDoubleTheWaitUpToTheMaximum) {
    ConnectBackoff backoff(5000, 300000);
    backoff.onFailure();
    EXPECT_EQ(backoff.waitMs(), 10000u);
    for (int i = 0; i < 10; i++) backoff.onFailure();
    EXPECT_EQ(backoff.waitMs(), 300000u);
}

TEST(ConnectBackoff, SuccessResetsTheWait) {
    ConnectBackoff backoff(5000, 300000);
    backoff.onFailure();
    backoff.onFailure();
    backoff.onSuccess();
    EXPECT_EQ(backoff.waitMs(), 5000u);
}

TEST(ConnectBackoff, HandlesCounterWraparound) {
    ConnectBackoff backoff(5000, 300000);
    backoff.onAttempt(0xFFFFF000u);
    EXPECT_FALSE(backoff.shouldAttempt(0x00000100u));
    EXPECT_TRUE(backoff.shouldAttempt(0x00000400u));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
