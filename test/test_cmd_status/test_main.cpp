#include <gtest/gtest.h>

#include <string>

#include "cmdStatus.h"

TEST(CmdStatus, TheGenericTableMatchesTheProtocol) {
    struct Row {
        uint8_t code;
        const char* name;
        const char* text;
    };
    const Row expected[] = {
        {1, "unknown", "Unknown command"},
        {2, "perm", "This command needs a signature"},
        {3, "auth", "The signature is not valid"},
        {4, "replay", "An older command than the last one. Not run"},
        {5, "arg", "Bad argument"},
        {6, "storage", "The node could not store the command counter"},
        {7, "no route", "The gateway has no route to the node"},
        {8, "timeout", "No reply within the time the gateway waited"},
        {9, "dropped", "Dropped by the gateway, too many commands waiting"},
        {10, "too long", "The command does not fit in one LoRa frame"},
        {11, "busy", "The node cannot run this now"},
        {12, "cancelled", "Cancelled before a reply arrived"},
    };
    size_t count = 0;
    const CommandStatusInfo* table = commandStatusTable(count);
    ASSERT_EQ(count, sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < count; i++) {
        EXPECT_EQ(table[i].code, expected[i].code);
        EXPECT_STREQ(table[i].name, expected[i].name);
        EXPECT_STREQ(table[i].text, expected[i].text);
        const CommandStatusInfo* info = commandStatusInfo(expected[i].code);
        ASSERT_NE(info, nullptr);
        EXPECT_STREQ(info->name, expected[i].name);
    }
}

TEST(CmdStatus, TheEnumFollowsTheTable) {
    EXPECT_EQ(static_cast<uint8_t>(CommandStatus::OK), 0);
    EXPECT_STREQ(commandStatusInfo(static_cast<uint8_t>(CommandStatus::UNKNOWN))->name, "unknown");
    EXPECT_STREQ(commandStatusInfo(static_cast<uint8_t>(CommandStatus::PERM))->name, "perm");
    EXPECT_STREQ(commandStatusInfo(static_cast<uint8_t>(CommandStatus::AUTH))->name, "auth");
    EXPECT_STREQ(commandStatusInfo(static_cast<uint8_t>(CommandStatus::REPLAY))->name, "replay");
    EXPECT_STREQ(commandStatusInfo(static_cast<uint8_t>(CommandStatus::ARG))->name, "arg");
    EXPECT_STREQ(commandStatusInfo(static_cast<uint8_t>(CommandStatus::STORAGE))->name, "storage");
    EXPECT_STREQ(commandStatusInfo(static_cast<uint8_t>(CommandStatus::NO_ROUTE))->name,
                 "no route");
    EXPECT_STREQ(commandStatusInfo(static_cast<uint8_t>(CommandStatus::TIMEOUT))->name, "timeout");
    EXPECT_STREQ(commandStatusInfo(static_cast<uint8_t>(CommandStatus::DROPPED))->name, "dropped");
    EXPECT_STREQ(commandStatusInfo(static_cast<uint8_t>(CommandStatus::TOO_LONG))->name,
                 "too long");
    EXPECT_STREQ(commandStatusInfo(static_cast<uint8_t>(CommandStatus::BUSY))->name, "busy");
    EXPECT_STREQ(commandStatusInfo(static_cast<uint8_t>(CommandStatus::CANCELLED))->name,
                 "cancelled");
}

TEST(CmdStatus, ACancelledCommandIsReportedWithoutArgument) {
    EXPECT_EQ(static_cast<uint8_t>(CommandStatus::CANCELLED), 12);
    EXPECT_EQ(formatReplyPayload(17, CommandResult(CommandStatus::CANCELLED)), "17 E12");
    EXPECT_EQ(describeCommandStatus(12, ""), "Cancelled before a reply arrived");
}

TEST(CmdStatus, CodesOutsideTheTableHaveNoRow) {
    EXPECT_EQ(commandStatusInfo(0), nullptr);
    EXPECT_EQ(commandStatusInfo(13), nullptr);
    EXPECT_EQ(commandStatusInfo(127), nullptr);
    EXPECT_EQ(commandStatusInfo(128), nullptr);
    EXPECT_EQ(commandStatusInfo(255), nullptr);
}

TEST(CmdStatus, TheCatalogueLineListsEveryGenericError) {
    EXPECT_EQ(commandStatusCatalogueLine(),
              "E|1=unknown;2=perm;3=auth;4=replay;5=arg;6=storage;7=no route;8=timeout;"
              "9=dropped;10=too long;11=busy;12=cancelled");
}

TEST(CmdStatus, FormatsTheErrorForm) {
    EXPECT_EQ(formatCommandError(7, ""), "E7");
    EXPECT_EQ(formatCommandError(8, "94"), "E8 94");
    EXPECT_EQ(formatCommandError(5, ""), "E5");
    EXPECT_EQ(formatCommandError(131, "REFUSE_BATTERY"), "E131 REFUSE_BATTERY");
    EXPECT_EQ(formatCommandError(255, ""), "E255");
}

TEST(CmdStatus, FormatsTheReplyPayload) {
    EXPECT_EQ(formatReplyPayload(17, CommandResult("OK install 600 s")), "17 OK install 600 s");
    EXPECT_EQ(formatReplyPayload(17, CommandResult(CommandStatus::NO_ROUTE)), "17 E7");
    EXPECT_EQ(formatReplyPayload(17, CommandResult(CommandStatus::TIMEOUT, "94")), "17 E8 94");
    EXPECT_EQ(formatReplyPayload(17, CommandResult(CommandStatus::ARG)), "17 E5");
    EXPECT_EQ(formatReplyPayload(0, CommandResult(128, "")), "0 E128");
    EXPECT_EQ(formatReplyPayload(255, CommandResult("")), "255 ");
}

TEST(CmdStatus, ParsesTheErrorForm) {
    uint8_t code = 0;
    std::string argument = "x";
    ASSERT_TRUE(parseCommandError("E7", code, argument));
    EXPECT_EQ(code, 7);
    EXPECT_EQ(argument, "");
    ASSERT_TRUE(parseCommandError("E8 94", code, argument));
    EXPECT_EQ(code, 8);
    EXPECT_EQ(argument, "94");
    ASSERT_TRUE(parseCommandError("E255 a b", code, argument));
    EXPECT_EQ(code, 255);
    EXPECT_EQ(argument, "a b");
}

TEST(CmdStatus, EveryCodeRoundTrips) {
    for (unsigned value = 1; value <= 255; value++) {
        uint8_t code = 0;
        std::string argument;
        ASSERT_TRUE(parseCommandError(formatCommandError(static_cast<uint8_t>(value), "arg"), code,
                                      argument))
            << value;
        EXPECT_EQ(code, value);
        EXPECT_EQ(argument, "arg");
    }
}

TEST(CmdStatus, PlainRepliesAreNotErrors) {
    uint8_t code = 99;
    std::string argument = "kept";
    for (const char* text : {"", "E", "OK", "Error 7", "ERR no route", "E0", "E256", "E1000", "E7x",
                             "e7", " E7", "E-1", "Usage: /x"}) {
        EXPECT_FALSE(parseCommandError(text, code, argument)) << text;
    }
    EXPECT_EQ(code, 99);
    EXPECT_EQ(argument, "kept");
}

TEST(CmdStatus, TellsATextThatStartsLikeAnError) {
    EXPECT_TRUE(looksLikeCommandError("E7"));
    EXPECT_TRUE(looksLikeCommandError("E0 text"));
    EXPECT_TRUE(looksLikeCommandError("E7x"));
    EXPECT_FALSE(looksLikeCommandError("Error"));
    EXPECT_FALSE(looksLikeCommandError("E"));
    EXPECT_FALSE(looksLikeCommandError("OK E7"));
    EXPECT_FALSE(looksLikeCommandError(""));
}

TEST(CmdStatus, ResultsBuildFromTextsAndStatuses) {
    CommandResult text = std::string("OK");
    EXPECT_TRUE(text.ok());
    EXPECT_EQ(text.text, "OK");
    CommandResult error = CommandStatus::BUSY;
    EXPECT_FALSE(error.ok());
    EXPECT_EQ(error.status, 11);
    EXPECT_EQ(error.text, "");
    CommandResult specific(200, "x");
    EXPECT_EQ(specific.status, 200);
    EXPECT_EQ(specific.text, "x");
}

TEST(CmdStatus, DescribesGenericErrorsForPeople) {
    EXPECT_EQ(describeCommandStatus(7, ""), "The gateway has no route to the node");
    EXPECT_EQ(describeCommandStatus(8, "94"), "No reply within the time the gateway waited (94 s)");
    EXPECT_EQ(describeCommandStatus(10, "101"),
              "The command does not fit in one LoRa frame (max 101 B)");
    EXPECT_EQ(describeCommandStatus(0, ""), "");
    EXPECT_EQ(describeCommandStatus(128, "x"), "");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
