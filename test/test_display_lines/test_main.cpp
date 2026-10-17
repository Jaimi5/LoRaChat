#include <gtest/gtest.h>

#include "displayLines.h"

TEST(DisplayLines, StartsWithEveryLineEmpty) {
    DisplayLines lines(3, 7);
    ASSERT_EQ(lines.size(), 7u);
    for (size_t i = 0; i < lines.size(); i++) EXPECT_EQ(lines.line(i), "");
}

TEST(DisplayLines, NewestTextGoesToTheFirstLogLine) {
    DisplayLines lines(3, 7);
    lines.push("one");
    lines.push("two");
    EXPECT_EQ(lines.line(3), "two");
    EXPECT_EQ(lines.line(4), "one");
}

TEST(DisplayLines, NeverGrowsBeyondMaxLines) {
    DisplayLines lines(3, 7);
    for (int i = 0; i < 10; i++) lines.push("line " + std::to_string(i));
    ASSERT_EQ(lines.size(), 7u);
    EXPECT_EQ(lines.line(3), "line 9");
    EXPECT_EQ(lines.line(6), "line 6");
}

TEST(DisplayLines, LogNeverOverwritesTheFixedLines) {
    DisplayLines lines(2, 4);
    lines.setStatic(0, "title");
    lines.setStatic(1, "id");
    for (int i = 0; i < 5; i++) lines.push("x");
    EXPECT_EQ(lines.line(0), "title");
    EXPECT_EQ(lines.line(1), "id");
}

TEST(DisplayLines, ClearKeepsTheFixedLines) {
    DisplayLines lines(2, 5);
    lines.setStatic(0, "title");
    lines.push("a");
    lines.clearLog();
    ASSERT_EQ(lines.size(), 5u);
    EXPECT_EQ(lines.line(0), "title");
    EXPECT_EQ(lines.line(2), "");
}

TEST(DisplayLines, TextAfterClearIsShown) {
    DisplayLines lines(3, 7);
    lines.push("before");
    lines.clearLog();
    lines.push("after");
    EXPECT_EQ(lines.line(3), "after");
}

TEST(DisplayLines, VersionChangesOnEveryChange) {
    DisplayLines lines(1, 3);
    uint32_t start = lines.version();
    lines.push("a");
    uint32_t pushed = lines.version();
    EXPECT_NE(pushed, start);
    lines.setStatic(0, "t");
    EXPECT_NE(lines.version(), pushed);
    uint32_t set = lines.version();
    lines.clearLog();
    EXPECT_NE(lines.version(), set);
}

TEST(DisplayLines, AllFixedLinesLeaveNoLog) {
    DisplayLines lines(3, 3);
    lines.push("dropped");
    for (size_t i = 0; i < lines.size(); i++) EXPECT_EQ(lines.line(i), "");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
