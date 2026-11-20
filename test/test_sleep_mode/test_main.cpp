#include <gtest/gtest.h>

#include "sleepMode.h"

TEST(SleepMode, Names) {
    EXPECT_STREQ(sleepModeName(SleepMode::NONE), "none");
    EXPECT_STREQ(sleepModeName(SleepMode::LIGHT), "light");
    EXPECT_STREQ(sleepModeName(SleepMode::DEEP), "deep");
}

TEST(SleepMode, ParsesNamesInAnyCase) {
    SleepMode mode = SleepMode::NONE;
    EXPECT_TRUE(parseSleepMode("light", mode));
    EXPECT_EQ(mode, SleepMode::LIGHT);
    EXPECT_TRUE(parseSleepMode("DEEP", mode));
    EXPECT_EQ(mode, SleepMode::DEEP);
    EXPECT_TRUE(parseSleepMode("None", mode));
    EXPECT_EQ(mode, SleepMode::NONE);
}

TEST(SleepMode, RejectsOtherText) {
    SleepMode mode = SleepMode::LIGHT;
    for (const char* bad : {"", "off", "lite", "deep ", "2"}) {
        EXPECT_FALSE(parseSleepMode(bad, mode)) << bad;
    }
    EXPECT_EQ(mode, SleepMode::LIGHT);
}

TEST(SleepMode, DecodesStoredValues) {
    SleepMode mode = SleepMode::NONE;
    EXPECT_TRUE(sleepModeFromValue(2, mode));
    EXPECT_EQ(mode, SleepMode::DEEP);
    EXPECT_TRUE(sleepModeFromValue(0, mode));
    EXPECT_EQ(mode, SleepMode::NONE);
    EXPECT_FALSE(sleepModeFromValue(3, mode));
    EXPECT_FALSE(sleepModeFromValue(0xFF, mode));
}

TEST(SleepFraction, ParsesWholePercentUpToTheMaximum) {
    uint8_t percent = 7;
    EXPECT_TRUE(parseSleepFraction("0", percent));
    EXPECT_EQ(percent, 0);
    EXPECT_TRUE(parseSleepFraction("90", percent));
    EXPECT_EQ(percent, 90);
    for (const char* bad :
         {"", "91", "100", "-1", "+5", "50%", "0.5", "abc", "5x", " 5", "4294967301"}) {
        EXPECT_FALSE(parseSleepFraction(bad, percent)) << bad;
    }
    EXPECT_EQ(percent, 90);
}

TEST(SleepDecision, AllowsTheConfiguredKindOnAnIdleSensor) {
    SleepBlockers idle;
    EXPECT_EQ(decideSleep(SleepMode::LIGHT, false, idle), SleepDecision::ALLOW);
    EXPECT_EQ(decideSleep(SleepMode::DEEP, true, idle), SleepDecision::ALLOW);
    EXPECT_EQ(decideSleep(SleepMode::DEEP, false, idle), SleepDecision::ALLOW);
}

TEST(SleepDecision, LightModeRefusesDeepSleep) {
    EXPECT_EQ(decideSleep(SleepMode::LIGHT, true, SleepBlockers{}), SleepDecision::NO_DEEP_SLEEP);
}

TEST(SleepDecision, ModeNoneNeverSleeps) {
    EXPECT_EQ(decideSleep(SleepMode::NONE, false, SleepBlockers{}), SleepDecision::MODE_NONE);
    EXPECT_EQ(decideSleep(SleepMode::NONE, true, SleepBlockers{}), SleepDecision::MODE_NONE);
}

TEST(SleepDecision, EachBlockerKeepsTheNodeAwake) {
    SleepBlockers gateway;
    gateway.gateway = true;
    SleepBlockers verifying;
    verifying.pendingVerify = true;
    SleepBlockers window;
    window.maintenanceWindow = true;
    SleepBlockers display;
    display.displayAwake = true;
    SleepBlockers pmu;
    pmu.pmuIrqLow = true;
    SleepBlockers install;
    install.installMode = true;
    SleepBlockers restart;
    restart.restartPending = true;
    for (bool deep : {false, true}) {
        EXPECT_EQ(decideSleep(SleepMode::DEEP, deep, restart), SleepDecision::RESTART_PENDING);
        EXPECT_EQ(decideSleep(SleepMode::LIGHT, deep, restart), SleepDecision::RESTART_PENDING);
        EXPECT_EQ(decideSleep(SleepMode::NONE, deep, restart), SleepDecision::MODE_NONE);
        EXPECT_EQ(decideSleep(SleepMode::DEEP, deep, install), SleepDecision::INSTALL_MODE);
        EXPECT_EQ(decideSleep(SleepMode::LIGHT, deep, install), SleepDecision::INSTALL_MODE);
        EXPECT_EQ(decideSleep(SleepMode::NONE, deep, install), SleepDecision::MODE_NONE);
        EXPECT_EQ(decideSleep(SleepMode::DEEP, deep, gateway), SleepDecision::GATEWAY);
        EXPECT_EQ(decideSleep(SleepMode::DEEP, deep, verifying), SleepDecision::PENDING_VERIFY);
        EXPECT_EQ(decideSleep(SleepMode::DEEP, deep, window), SleepDecision::MAINTENANCE_WINDOW);
        EXPECT_EQ(decideSleep(SleepMode::DEEP, deep, display), SleepDecision::DISPLAY_AWAKE);
        EXPECT_EQ(decideSleep(SleepMode::DEEP, deep, pmu), SleepDecision::PMU_IRQ_PENDING);
        EXPECT_EQ(decideSleep(SleepMode::LIGHT, deep, pmu), SleepDecision::PMU_IRQ_PENDING);
    }
}

TEST(SleepDecision, NamesForTheLog) {
    EXPECT_STREQ(sleepDecisionName(SleepDecision::ALLOW), "allow");
    EXPECT_STREQ(sleepDecisionName(SleepDecision::MAINTENANCE_WINDOW), "maintenance window");
    EXPECT_STREQ(sleepDecisionName(SleepDecision::PMU_IRQ_PENDING), "pmu interrupt pending");
    EXPECT_STREQ(sleepDecisionName(SleepDecision::INSTALL_MODE), "install mode");
    EXPECT_EQ(static_cast<uint8_t>(SleepDecision::RESTART_PENDING), 9);
    EXPECT_STREQ(sleepDecisionName(SleepDecision::RESTART_PENDING), "restart pending");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
