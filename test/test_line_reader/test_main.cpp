#include <gtest/gtest.h>

#include <vector>

#include "lineReader.h"

namespace {

std::vector<std::string> feed(LineReader& reader, const std::string& bytes) {
    std::vector<std::string> lines;
    std::string line;
    for (char c : bytes) {
        if (reader.push(c, line)) lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST(LineReader, SplitsOnLfCrAndCrlf) {
    LineReader reader;
    EXPECT_EQ(feed(reader, "/role\n/help\r/role.set sensor\r\n"),
              (std::vector<std::string>{"/role", "/help", "/role.set sensor"}));
}

TEST(LineReader, TrimsSpacesAndIgnoresEmptyLines) {
    LineReader reader;
    EXPECT_EQ(feed(reader, "\r\n\n   \n  /role  \n"), (std::vector<std::string>{"/role"}));
}

TEST(LineReader, KeepsPartialLineUntilItEnds) {
    LineReader reader;
    EXPECT_TRUE(feed(reader, "/ro").empty());
    EXPECT_EQ(feed(reader, "le\n"), (std::vector<std::string>{"/role"}));
}

TEST(LineReader, DropsALineThatIsTooLong) {
    LineReader reader;
    std::string tooLong(LineReader::MAX_LINE + 1, 'x');
    EXPECT_EQ(feed(reader, tooLong + "\n/role\n"), (std::vector<std::string>{"/role"}));
}

TEST(LineReader, AcceptsALineOfMaximumLength) {
    LineReader reader;
    std::string longest(LineReader::MAX_LINE, 'x');
    EXPECT_EQ(feed(reader, longest + "\n"), (std::vector<std::string>{longest}));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
