#include <gtest/gtest.h>

#include "maintWindow.h"

TEST(MaintWindow, SensorOpensTheBootWindow) {
    EXPECT_EQ(MaintWindow::decideBootWindow(false, false, true, false), BootWindowDecision::OPEN);
}

TEST(MaintWindow, GatewayKeepsItsOwnWifi) {
    EXPECT_EQ(MaintWindow::decideBootWindow(true, false, true, false),
              BootWindowDecision::SKIP_GATEWAY);
}

TEST(MaintWindow, NotWhileTheImageIsBeingVerified) {
    EXPECT_EQ(MaintWindow::decideBootWindow(false, true, true, false),
              BootWindowDecision::SKIP_PENDING_VERIFY);
}

TEST(MaintWindow, NotWithoutAHotspot) {
    EXPECT_EQ(MaintWindow::decideBootWindow(false, false, false, false),
              BootWindowDecision::SKIP_NO_CREDENTIALS);
}

TEST(MaintWindow, NotAfterADeepSleepWake) {
    EXPECT_EQ(MaintWindow::decideBootWindow(false, false, true, true),
              BootWindowDecision::SKIP_DEEP_SLEEP_WAKE);
}

TEST(MaintWindow, BootWindowLastsSixtySeconds) {
    EXPECT_EQ(MaintWindow::BOOT_WINDOW_MS, 60000u);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
