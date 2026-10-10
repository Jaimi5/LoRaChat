#include <gtest/gtest.h>

#include <cstring>

#include "displayText.h"

TEST(DisplayTextFrom, StopsAtTheFirstNul) {
    const char text[] = "hello\0world";
    EXPECT_EQ(displayTextFrom(text, sizeof(text)), "hello");
}

TEST(DisplayTextFrom, ReadsOnlyTheReceivedBytesWhenThereIsNoNul) {
    char text[16];
    std::memset(text, 'x', sizeof(text));
    std::memcpy(text, "hi there", 8);
    EXPECT_EQ(displayTextFrom(text, 8), "hi there");
}

TEST(DisplayTextFrom, EmptyPayloadGivesEmptyText) {
    EXPECT_EQ(displayTextFrom("abc", 0), "");
    EXPECT_EQ(displayTextFrom(nullptr, 5), "");
}

TEST(DisplayTextLimit, LeavesRoomForTheCommandByteAndTheNul) {
    EXPECT_EQ(displayTextLimit(102, 128), 100u);
}

TEST(DisplayTextLimit, IsBoundedByTheTextBuffer) {
    EXPECT_EQ(displayTextLimit(242, 128), 127u);
}

TEST(DisplayTextLimit, IsZeroWhenNothingFits) {
    EXPECT_EQ(displayTextLimit(1, 128), 0u);
    EXPECT_EQ(displayTextLimit(0, 128), 0u);
    EXPECT_EQ(displayTextLimit(102, 0), 0u);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
