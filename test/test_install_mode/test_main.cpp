#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "installMode.h"

TEST(InstallMode, OffUntilStarted) {
    InstallMode mode;
    EXPECT_FALSE(mode.active(0));
    EXPECT_FALSE(mode.active(123456));
    EXPECT_EQ(mode.remainingS(123456), 0U);
    EXPECT_EQ(mode.remainingMs(123456), 0U);
}

TEST(InstallMode, RunsForTheRequestedTimeAndEndsByItself) {
    InstallMode mode;
    EXPECT_EQ(mode.start(600, 1000), 600U);
    EXPECT_TRUE(mode.active(1000));
    EXPECT_EQ(mode.remainingS(1000), 600U);
    EXPECT_EQ(mode.remainingS(1001), 600U);
    EXPECT_EQ(mode.remainingS(2000), 599U);
    EXPECT_TRUE(mode.active(600999));
    EXPECT_EQ(mode.remainingS(600999), 1U);
    EXPECT_EQ(mode.remainingMs(600999), 1U);
    EXPECT_FALSE(mode.active(601000));
    EXPECT_EQ(mode.remainingS(601000), 0U);
    EXPECT_FALSE(mode.active(9999999));
}

TEST(InstallMode, ASecondStartSetsANewEnd) {
    InstallMode mode;
    mode.start(600, 0);
    EXPECT_EQ(mode.start(60, 300000), 60U);
    EXPECT_EQ(mode.remainingS(300000), 60U);
    EXPECT_FALSE(mode.active(360000));
    EXPECT_EQ(mode.start(900, 360000), 900U);
    EXPECT_TRUE(mode.active(1259999));
}

TEST(InstallMode, StopAndZeroEndIt) {
    InstallMode mode;
    mode.start(600, 0);
    mode.stop();
    EXPECT_FALSE(mode.active(1));
    mode.start(600, 0);
    EXPECT_EQ(mode.start(0, 5000), 0U);
    EXPECT_FALSE(mode.active(5000));
    EXPECT_EQ(mode.remainingS(5000), 0U);
}

TEST(InstallMode, DurationIsLimited) {
    InstallMode mode;
    EXPECT_EQ(mode.start(0xFFFFFFFF, 0), INSTALL_MAX_SECONDS);
    EXPECT_EQ(mode.remainingS(0), 3600U);
    EXPECT_FALSE(mode.active(3600000));
}

TEST(InstallMode, WorksLateInTheLifeOfTheClock) {
    InstallMode mode;
    uint64_t now = 0xFFFFFFFFULL - 100;
    mode.start(10, now);
    EXPECT_TRUE(mode.active(now + 9999));
    EXPECT_FALSE(mode.active(now + 10000));
}

TEST(InstallMode, ReportIntervalFollowsTheSuperframe) {
    EXPECT_EQ(InstallMode::reportIntervalMs(0), 10000U);
    EXPECT_EQ(InstallMode::reportIntervalMs(4999), 10000U);
    EXPECT_EQ(InstallMode::reportIntervalMs(5000), 10000U);
    EXPECT_EQ(InstallMode::reportIntervalMs(5001), 10002U);
    EXPECT_EQ(InstallMode::reportIntervalMs(15200), 30400U);
    EXPECT_EQ(InstallMode::reportIntervalMs(89300), 178600U);
    EXPECT_EQ(InstallMode::reportIntervalMs(0xFFFFFFFF), 0xFFFFFFFEU);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
