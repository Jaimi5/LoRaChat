#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <vector>

#include "cmdCatalogue.h"
#include "cmdStatus.h"
#include "firmwareCatalogue.h"
// The fixture reads limits from NetCore and OtaCore. These includes let PlatformIO find both.
#include "installMode.h"
#include "maintWindowPlan.h"

namespace {

const char* const GENERIC_LINE =
    "E|1=unknown;2=perm;3=auth;4=replay;5=arg;6=storage;7=no route;8=timeout;9=dropped;"
    "10=too long;11=busy;12=cancelled;13=not remote";

std::vector<std::string> lines(const std::string& page) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= page.size()) {
        size_t end = page.find('\n', start);
        if (end == std::string::npos) end = page.size();
        out.push_back(page.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= line.size()) {
        size_t end = line.find('|', start);
        if (end == std::string::npos) end = line.size();
        out.push_back(line.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

}  // namespace

TEST(CmdCatalogue, PermNames) {
    EXPECT_STREQ(commandPermName(Perm::OPEN), "open");
    EXPECT_STREQ(commandPermName(Perm::SIGNED), "signed");
    EXPECT_STREQ(commandPermName(Perm::LOCAL_ONLY), "local");
}

TEST(CmdCatalogue, ANameRegisteredTwiceIsRefused) {
    CommandCatalogue catalogue;
    EXPECT_TRUE(catalogue.add(spec("/version", Perm::OPEN)));
    EXPECT_TRUE(catalogue.add(spec("/reboot", Perm::SIGNED)));
    // A second service brings a name that is taken.
    EXPECT_FALSE(catalogue.add(spec("/version", Perm::SIGNED, "other")));
    EXPECT_EQ(catalogue.size(), 2U);
    ASSERT_NE(catalogue.find("/version"), nullptr);
    EXPECT_EQ(catalogue.find("/version")->perm, Perm::OPEN);
    EXPECT_EQ(catalogue.find("/version")->usage, "");
}

TEST(CmdCatalogue, NamesAreComparedIgnoringCase) {
    CommandCatalogue catalogue;
    EXPECT_TRUE(catalogue.add(spec("/getRT", Perm::OPEN)));
    EXPECT_FALSE(catalogue.add(spec("/getrt", Perm::OPEN)));
    EXPECT_FALSE(catalogue.add(spec("/GETRT", Perm::OPEN)));
    EXPECT_NE(catalogue.find("/GetRt"), nullptr);
    EXPECT_EQ(catalogue.find("/getR"), nullptr);
    EXPECT_TRUE(sameCommandName("/Key.Set", "/key.set"));
    EXPECT_FALSE(sameCommandName("/key", "/key.set"));
}

TEST(CmdCatalogue, BadSpecsAreRefused) {
    CommandCatalogue catalogue;
    EXPECT_FALSE(catalogue.add(spec("", Perm::OPEN)));
    // Codes below 128 belong to the generic table.
    EXPECT_FALSE(catalogue.add(spec("/a", Perm::OPEN, "", {{127, "generic"}})));
    EXPECT_FALSE(catalogue.add(spec("/b", Perm::OPEN, "", {{128, "one"}, {128, "two"}})));
    EXPECT_EQ(catalogue.size(), 0U);
    EXPECT_TRUE(catalogue.add(spec("/c", Perm::OPEN, "", {{128, "one"}, {255, "two"}})));
}

TEST(CmdCatalogue, GivesTheTextOfASpecificCode) {
    CommandCatalogue catalogue;
    catalogue.add(spec("/sensor", Perm::OPEN, "", {{128, "No sensor found"}}));
    catalogue.add(spec("/getIP", Perm::OPEN, "", {{128, "The node has no IP address"}}));
    EXPECT_EQ(catalogue.codeText("/sensor", 128), "No sensor found");
    EXPECT_EQ(catalogue.codeText("/SENSOR", 128), "No sensor found");
    EXPECT_EQ(catalogue.codeText("/getIP", 128), "The node has no IP address");
    EXPECT_EQ(catalogue.codeText("/sensor", 129), "");
    EXPECT_EQ(catalogue.codeText("/other", 128), "");
}

TEST(CmdCatalogue, ACommandTakesOneLine) {
    EXPECT_EQ(CommandCatalogue::line(spec("/version", Perm::OPEN)), "/version|open||");
    EXPECT_EQ(CommandCatalogue::line(spec("/install", Perm::SIGNED,
                                          "[seconds] (default 600, 0 stops, up to 3600)",
                                          {{128, "text one"}, {129, "text two"}})),
              "/install|signed|[seconds] (default 600, 0 stops, up to 3600)|128=text one;"
              "129=text two");
    EXPECT_EQ(CommandCatalogue::line(spec("/key.set", Perm::LOCAL_ONLY, "<64 hex digits>")),
              "/key.set|local|<64 hex digits>|");
}

TEST(CmdCatalogue, SeparatorsNeverAppearInsideAField) {
    EXPECT_EQ(CommandCatalogue::field("gateway|sensor"), "gateway/sensor");
    EXPECT_EQ(CommandCatalogue::field("a;b"), "a,b");
    EXPECT_EQ(CommandCatalogue::field("one\ntwo\r\nthree"), "one two  three");
    EXPECT_EQ(CommandCatalogue::field("plain text"), "plain text");
    std::string line = CommandCatalogue::line(
        spec("/x|y", Perm::OPEN, "a|b;c\nd", {{128, "no|pipe;or\nbreak"}, {129, "two"}}));
    EXPECT_EQ(line, "/x/y|open|a/b,c d|128=no/pipe,or break;129=two");
    EXPECT_EQ(fields(line).size(), 4U);
}

TEST(CmdCatalogue, AnEmptyCatalogueHasOnePageWithTheGenericErrors) {
    CommandCatalogue catalogue;
    std::vector<std::string> pages = catalogue.pages("1.2.3");
    ASSERT_EQ(pages.size(), 1U);
    EXPECT_EQ(pages[0], std::string("cmds 1 1.2.3 1/1\n") + GENERIC_LINE);
}

TEST(CmdCatalogue, TheFirstLinesAreExact) {
    CommandCatalogue catalogue;
    catalogue.add(spec("/version", Perm::OPEN));
    catalogue.add(spec("/install", Perm::SIGNED, "[seconds] (default 600, 0 stops, up to 3600)",
                       {{128, "text one"}, {129, "text two"}}));
    std::vector<std::string> pages = catalogue.pages("0.1.2+g2bfdeeb.dirty");
    ASSERT_EQ(pages.size(), 1U);
    EXPECT_EQ(pages[0],
              std::string("cmds 1 0.1.2+g2bfdeeb.dirty 1/1\n") + GENERIC_LINE +
                  "\n/install|signed|[seconds] (default 600, 0 stops, up to 3600)|128=text one;"
                  "129=text two\n/version|open||");
}

TEST(CmdCatalogue, TheVersionHasNoSpaces) {
    CommandCatalogue catalogue;
    EXPECT_EQ(lines(catalogue.pages("0.1.2 test build")[0])[0], "cmds 1 0.1.2_test_build 1/1");
    EXPECT_EQ(lines(catalogue.pages("a|b;c")[0])[0], "cmds 1 a/b,c 1/1");
}

TEST(CmdCatalogue, CommandsAreSortedByName) {
    CommandCatalogue catalogue;
    for (const char* name : {"/version", "/sleep.set", "/getRT", "/sleep", "/addSSID", "/getIP",
                             "/sleep.fraction", "/addPassword"}) {
        catalogue.add(spec(name, Perm::OPEN));
    }
    std::vector<std::string> all = lines(catalogue.pages("v")[0]);
    ASSERT_EQ(all.size(), 10U);
    const char* expected[] = {"/addPassword", "/addSSID",        "/getIP",     "/getRT",
                              "/sleep",       "/sleep.fraction", "/sleep.set", "/version"};
    for (size_t i = 0; i < 8; i++) EXPECT_EQ(fields(all[i + 2])[0], expected[i]);
}

TEST(CmdCatalogue, PagesStayUnderTheLimitAndKeepEveryCommandOnce) {
    CommandCatalogue catalogue;
    for (int i = 0; i < 60; i++) {
        std::string name = "/command" + std::string(i < 10 ? "0" : "") + std::to_string(i);
        catalogue.add(spec(name, i % 2 == 0 ? Perm::OPEN : Perm::SIGNED,
                           "<argument number " + std::to_string(i) + ">",
                           {{128, "It did not work"}, {129, "It did not work either"}}));
    }
    for (size_t limit : {size_t{300}, size_t{512}, CATALOGUE_PAGE_MAX_BYTES}) {
        std::vector<std::string> pages = catalogue.pages("0.1.2+g2bfdeeb", limit);
        ASSERT_GT(pages.size(), 1U);
        std::vector<std::string> names;
        for (size_t i = 0; i < pages.size(); i++) {
            EXPECT_LE(pages[i].size(), limit) << "page " << i + 1;
            std::vector<std::string> all = lines(pages[i]);
            EXPECT_EQ(all[0], "cmds 1 0.1.2+g2bfdeeb " + std::to_string(i + 1) + "/" +
                                  std::to_string(pages.size()));
            // The generic errors come on page 1 only.
            size_t first = 1;
            if (i == 0) {
                EXPECT_EQ(all[1], GENERIC_LINE);
                first = 2;
            }
            ASSERT_GT(all.size(), first);
            for (size_t j = first; j < all.size(); j++) {
                ASSERT_EQ(fields(all[j]).size(), 4U) << all[j];
                EXPECT_EQ(all[j][0], '/');
                names.push_back(fields(all[j])[0]);
            }
        }
        ASSERT_EQ(names.size(), 60U);
        for (size_t i = 1; i < names.size(); i++) EXPECT_LT(names[i - 1], names[i]);
    }
}

TEST(CmdCatalogue, PagesAreStable) {
    CommandCatalogue a = firmwareCatalogue();
    EXPECT_EQ(a.pages("0.1.2"), a.pages("0.1.2"));
}

TEST(CmdCatalogue, ACommandLongerThanAPageStillGetsOne) {
    CommandCatalogue catalogue;
    catalogue.add(spec("/a", Perm::OPEN));
    catalogue.add(spec("/b", Perm::OPEN, std::string(400, 'x')));
    catalogue.add(spec("/c", Perm::OPEN));
    std::vector<std::string> pages = catalogue.pages("v", 200);
    ASSERT_EQ(pages.size(), 3U);
    EXPECT_EQ(lines(pages[0]).back(), "/a|open||");
    EXPECT_EQ(lines(pages[2]).back(), "/c|open||");
}

TEST(CmdCatalogue, ReadsThePageArgument) {
    size_t page = 0;
    EXPECT_TRUE(parseCataloguePage("", 3, page));
    EXPECT_EQ(page, 1U);
    EXPECT_TRUE(parseCataloguePage("1", 3, page));
    EXPECT_EQ(page, 1U);
    EXPECT_TRUE(parseCataloguePage("3", 3, page));
    EXPECT_EQ(page, 3U);
    page = 99;
    for (const char* bad :
         {"0", "4", "-1", "+1", "x", "2x", "1 2", "1.0", " 2", "0002", "9999999999"}) {
        EXPECT_FALSE(parseCataloguePage(bad, 3, page)) << bad;
    }
    EXPECT_EQ(page, 99U);
}

// The real catalogue of a gateway. The web is coded against this output.
TEST(CmdCatalogue, TheFirmwareCatalogueFitsItsPages) {
    CommandCatalogue catalogue = firmwareCatalogue();
    const std::string version = "0.1.2+g2bfdeeb.dirty";
    std::vector<std::string> pages = catalogue.pages(version);
    size_t commands = 0;
    for (size_t i = 0; i < pages.size(); i++) {
        std::cout << "--- /cmds " << i + 1 << " (" << pages[i].size() << " bytes)\n"
                  << pages[i] << "\n";
        // With the longest request id the MQTT payload stays under 900 bytes.
        EXPECT_LT(formatReplyPayload(255, CommandResult(pages[i])).size(), 900U);
        EXPECT_FALSE(looksLikeCommandError(pages[i]));
        commands += lines(pages[i]).size() - (i == 0 ? 2 : 1);
    }
    EXPECT_EQ(commands, catalogue.size());
    std::vector<std::string> first = lines(pages[0]);
    EXPECT_EQ(first[0], "cmds 1 " + version + " 1/" + std::to_string(pages.size()));
    EXPECT_EQ(first[1], GENERIC_LINE);
    EXPECT_EQ(first[2], "/addPassword|local|<password of at most 64 bytes>|");
    EXPECT_EQ(first[3], "/addSSID|local|<ssid of 1 to 32 bytes>|");
    EXPECT_EQ(first[4], "/cmds|open|[page]|");
    // A command with an id appends its id, its argument schema and its reply.
    EXPECT_EQ(first[5], "/connectWiFi|signed||128=The WiFi did not connect|50||=Connected");
    EXPECT_EQ(lines(pages.back()).back(),
              "/version|open|||34||ver:v slot:e(app0,app1)? state:e(VALID,PENDING_VERIFY) bl:h3 "
              "env:e(tbeam,tbeam-debug)? git:h4 dirty:e(dirty)?={0}+g{5}[.{6}][ {1}] {2} "
              "bl={3}[ env={4}]");
    EXPECT_EQ(pages.size(), 8U);
}

TEST(CmdCatalogue, ACommandWithAnIdAppendsThreeFields) {
    CommandCatalogue catalogue = firmwareCatalogue();
    ASSERT_NE(catalogue.find("/sensor"), nullptr);
    std::string sensor = CommandCatalogue::line(*catalogue.find("/sensor"));
    EXPECT_EQ(sensor,
              "/sensor|open||128=No sensor found (BME280 or BMP280 at 0x76 or 0x77);129=The "
              "sensor gave no measurement;130=This firmware has no sensor support|18||"
              "chip:e(BMP280,BME280) addr:h1 t:i/100 p:u/100 rh:u?/100 age:u?={0} at 0x{1}: {2} "
              "C[ {4} %RH] {3} hPa[, read {5} s ago, the sensor does not answer now]");
    EXPECT_EQ(fields(sensor).size(), 7U);
    ASSERT_NE(catalogue.find("/report.set"), nullptr);
    EXPECT_EQ(CommandCatalogue::line(*catalogue.find("/report.set")),
              "/report.set|signed|<seconds from 60 to 43200, or 0 for the default>|128=The "
              "interval could not be stored|7a|seconds:u|stored:u? def:u?=Report interval set to "
              "[{0} s][default ({1} s)]");
    // The first four fields are the line of the same command without an id.
    CommandSpec plain = *catalogue.find("/report.set");
    plain.id = 0;
    plain.args = plain.reply = plain.replyTemplate = "";
    EXPECT_EQ(CommandCatalogue::line(*catalogue.find("/report.set"))
                  .compare(0, CommandCatalogue::line(plain).size(), CommandCatalogue::line(plain)),
              0);
    // A command without an id keeps four fields.
    ASSERT_NE(catalogue.find("/help"), nullptr);
    EXPECT_EQ(CommandCatalogue::line(*catalogue.find("/help")), "/help|open||");
}

TEST(CmdCatalogue, AnIdRegisteredTwiceIsRefused) {
    CommandCatalogue catalogue;
    EXPECT_TRUE(catalogue.add(typed(0x18, spec("/sensor", Perm::OPEN), "", "", "")));
    EXPECT_TRUE(catalogue.add(typed(0x34, spec("/version", Perm::OPEN), "", "", "")));
    // A second command brings an id that is taken.
    EXPECT_FALSE(catalogue.add(typed(0x18, spec("/other", Perm::OPEN), "", "", "")));
    EXPECT_EQ(catalogue.size(), 2U);
    EXPECT_EQ(catalogue.find("/other"), nullptr);
    // Commands without an id do not collide with each other.
    EXPECT_TRUE(catalogue.add(spec("/a", Perm::OPEN)));
    EXPECT_TRUE(catalogue.add(spec("/b", Perm::OPEN)));
}

TEST(CmdCatalogue, FindsACommandByItsId) {
    CommandCatalogue catalogue = firmwareCatalogue();
    ASSERT_NE(catalogue.findById(0x18), nullptr);
    EXPECT_EQ(catalogue.findById(0x18)->name, "/sensor");
    ASSERT_NE(catalogue.findById(0x7A), nullptr);
    EXPECT_EQ(catalogue.findById(0x7A)->name, "/report.set");
    EXPECT_EQ(catalogue.findById(0x19), nullptr);
    // Id 0 stands for no id, which most commands have.
    EXPECT_EQ(catalogue.findById(0), nullptr);
}

TEST(CmdCatalogue, ATypedDeclarationThatIsNotValidIsRefused) {
    CommandCatalogue catalogue;
    // Schemas and a template need an id.
    EXPECT_FALSE(catalogue.add(typed(0, spec("/a", Perm::OPEN), "seconds:u", "", "")));
    EXPECT_FALSE(catalogue.add(typed(0, spec("/a", Perm::OPEN), "", "", "Done")));
    EXPECT_FALSE(catalogue.add(typed(0x20, spec("/a", Perm::OPEN), "seconds:x", "", "")));
    EXPECT_FALSE(catalogue.add(typed(0x20, spec("/a", Perm::OPEN), "", "t:i/7", "")));
    // The template names a field the reply does not have.
    EXPECT_FALSE(catalogue.add(typed(0x20, spec("/a", Perm::OPEN), "", "t:i", "{1}")));
    EXPECT_FALSE(catalogue.add(typed(0x20, spec("/a", Perm::OPEN), "", "", "{a0}")));
    // A line of /cmds cannot carry these characters.
    EXPECT_FALSE(catalogue.add(typed(0x20, spec("/a", Perm::OPEN), "", "t:i", "a|b {0}")));
    EXPECT_FALSE(catalogue.add(typed(0x20, spec("/a", Perm::OPEN), "", "t:i", "a; {0}")));
    EXPECT_FALSE(catalogue.add(typed(0x20, spec("/a", Perm::OPEN), "", "t:i", "a\n{0}")));
    EXPECT_EQ(catalogue.size(), 0U);
    EXPECT_TRUE(catalogue.add(typed(0x20, spec("/a", Perm::OPEN), "n:u", "t:i", "{a0} {0}")));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
