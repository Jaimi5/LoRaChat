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

TEST(ParseInstallSeconds, DefaultAndRange) {
    uint32_t seconds = 7;
    EXPECT_TRUE(parseInstallSeconds("", seconds));
    EXPECT_EQ(seconds, 600U);
    EXPECT_TRUE(parseInstallSeconds("0", seconds));
    EXPECT_EQ(seconds, 0U);
    EXPECT_TRUE(parseInstallSeconds("3600", seconds));
    EXPECT_EQ(seconds, 3600U);
    EXPECT_TRUE(parseInstallSeconds("0045", seconds));
    EXPECT_EQ(seconds, 45U);
}

TEST(ParseInstallSeconds, RefusesEverythingElse) {
    uint32_t seconds = 7;
    for (const char* text :
         {"3601", "9999", "10000", "-1", "+5", "1.5", "10 s", " 10", "ten", "0x10", "4294967297"}) {
        EXPECT_FALSE(parseInstallSeconds(text, seconds)) << text;
        EXPECT_EQ(seconds, 7U);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
