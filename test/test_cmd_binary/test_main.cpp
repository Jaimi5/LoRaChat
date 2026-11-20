#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cmdAuth.h"
#include "cmdCatalogue.h"
#include "cmdFields.h"
#include "cmdFrame.h"
#include "cmdText.h"
#include "cmdTyped.h"
#include "firmwareCatalogue.h"
#include "typedFrames.h"
// The fixture reads limits from NetCore and OtaCore. These includes let PlatformIO find both.
#include "installMode.h"
#include "maintWindowPlan.h"

namespace {

using Bytes = std::vector<uint8_t>;

TypedDeclaration declared(uint8_t id, const std::string& args, const std::string& reply,
                          const std::string& replyTemplate) {
    TypedDeclaration declaration;
    EXPECT_TRUE(parseTypedDeclaration(id, args, reply, replyTemplate, declaration));
    return declaration;
}

std::string readGoldenIds() {
    // The tests run from the project folder, or from anywhere with the path of this file.
    const std::string here = __FILE__;
    const std::string beside = here.substr(0, here.find("test_cmd_binary")) + "vectors/cmdIds.txt";
    for (const std::string& path : {std::string("test/vectors/cmdIds.txt"), beside}) {
        std::ifstream file(path);
        if (!file) continue;
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }
    ADD_FAILURE() << "test/vectors/cmdIds.txt not found";
    return "";
}

}  // namespace

// --- Frames ---

TEST(BinaryFrame, AnUnsignedRequestIsFlagsIdAndFields) {
    BinaryCommandFrame frame;
    frame.id = 0x7A;
    frame.payload = {0xD8, 0x04};
    Bytes bytes = frame.encode();
    EXPECT_EQ(bytes, (Bytes{0x20, 0x7A, 0xD8, 0x04}));

    BinaryCommandFrame out;
    ASSERT_TRUE(BinaryCommandFrame::decode(bytes.data(), bytes.size(), out));
    EXPECT_FALSE(out.response);
    EXPECT_FALSE(out.isSigned);
    EXPECT_EQ(out.id, 0x7A);
    EXPECT_EQ(out.payload, (Bytes{0xD8, 0x04}));
}

TEST(BinaryFrame, ASignedRequestEndsWithCounterAndTag) {
    BinaryCommandFrame frame;
    frame.id = 0x13;
    frame.isSigned = true;
    frame.payload = {0x01};
    frame.counter = GOLDEN_COUNTER;
    frame.tag = GOLDEN_TAG;
    Bytes bytes = frame.encode();
    EXPECT_EQ(bytes, (Bytes{0x60, 0x13, 0x01, 0x04, 0x03, 0x02, 0x01, 0x11, 0x22, 0x33, 0x44, 0x55,
                            0x66, 0x77, 0x88}));

    BinaryCommandFrame out;
    ASSERT_TRUE(BinaryCommandFrame::decode(bytes.data(), bytes.size(), out));
    EXPECT_TRUE(out.isSigned);
    EXPECT_EQ(out.id, 0x13);
    EXPECT_EQ(out.payload, (Bytes{0x01}));
    EXPECT_EQ(out.counter, GOLDEN_COUNTER);
    EXPECT_EQ(out.tag, GOLDEN_TAG);
}

TEST(BinaryFrame, AReplyIsFlagsStatusAndFields) {
    BinaryCommandFrame frame;
    frame.response = true;
    // A reply carries no command id.
    frame.id = 0x34;
    EXPECT_EQ(frame.encode(), (Bytes{0xA0, 0x00}));
    frame.status = 128;
    frame.payload = {0x85, 0x22};
    Bytes bytes = frame.encode();
    EXPECT_EQ(bytes, (Bytes{0xA0, 0x80, 0x85, 0x22}));

    BinaryCommandFrame out;
    ASSERT_TRUE(BinaryCommandFrame::decode(bytes.data(), bytes.size(), out));
    EXPECT_TRUE(out.response);
    EXPECT_FALSE(out.replayed);
    EXPECT_EQ(out.id, 0);
    EXPECT_EQ(out.status, 128);
    EXPECT_EQ(out.payload, (Bytes{0x85, 0x22}));
}

TEST(BinaryFrame, BitFourMarksAReplyLostInARestart) {
    BinaryCommandFrame frame;
    frame.response = true;
    frame.replayed = true;
    Bytes bytes = frame.encode();
    EXPECT_EQ(bytes, (Bytes{0xB0, 0x00}));
    BinaryCommandFrame out;
    ASSERT_TRUE(BinaryCommandFrame::decode(bytes.data(), bytes.size(), out));
    EXPECT_TRUE(out.replayed);
    // The bit means nothing in a request.
    const Bytes request = {0x30, 0x34};
    ASSERT_TRUE(BinaryCommandFrame::decode(request.data(), request.size(), out));
    EXPECT_FALSE(out.replayed);
    BinaryCommandFrame asRequest;
    asRequest.id = 0x34;
    asRequest.replayed = true;
    EXPECT_EQ(asRequest.encode(), (Bytes{0x20, 0x34}));
}

TEST(BinaryFrame, RefusesWhatIsNotABinaryFrame) {
    BinaryCommandFrame out;
    out.id = 0x55;
    EXPECT_FALSE(BinaryCommandFrame::decode(nullptr, 0, out));
    for (const Bytes& bad : std::vector<Bytes>{
             {0x20},
             // Bit 5 clear: a text frame.
             {0x00, 0x34},
             {0x80, 0x00},
             // A reply is never signed.
             {0xE0, 0x00},
             // Id 0 stands for no id.
             {0x20, 0x00},
             // Signed, with less than counter and tag behind the id.
             {0x60, 0x13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
         }) {
        EXPECT_FALSE(BinaryCommandFrame::decode(bad.data(), bad.size(), out));
    }
    EXPECT_EQ(out.id, 0x55);
}

TEST(BinaryFrame, BitFiveTellsTheTwoFormsApart) {
    CommandFrame text;
    text.line = "/version";
    Bytes textBytes = text.encode();
    EXPECT_FALSE(isBinaryCommandFrame(textBytes.data(), textBytes.size()));
    text.isSigned = true;
    textBytes = text.encode();
    EXPECT_FALSE(isBinaryCommandFrame(textBytes.data(), textBytes.size()));
    CommandFrame textReply;
    textReply.response = true;
    textReply.line = "ok";
    textBytes = textReply.encode();
    EXPECT_FALSE(isBinaryCommandFrame(textBytes.data(), textBytes.size()));

    BinaryCommandFrame binary;
    binary.id = 0x34;
    Bytes binaryBytes = binary.encode();
    EXPECT_TRUE(isBinaryCommandFrame(binaryBytes.data(), binaryBytes.size()));
    binary.response = true;
    binaryBytes = binary.encode();
    EXPECT_TRUE(isBinaryCommandFrame(binaryBytes.data(), binaryBytes.size()));
    EXPECT_FALSE(isBinaryCommandFrame(nullptr, 0));
}

TEST(BinaryFrame, TheTextFrameIsUnchanged) {
    CommandFrame frame;
    frame.line = "/version";
    EXPECT_EQ(frame.encode(), (Bytes{0x00, '/', 'v', 'e', 'r', 's', 'i', 'o', 'n'}));
    CommandFrame reply;
    reply.response = true;
    reply.status = 5;
    reply.line = "x";
    EXPECT_EQ(reply.encode(), (Bytes{0x80, 0x05, 'x'}));
}

// --- Declarations ---

TEST(TypedDeclarations, ReadsADeclaration) {
    TypedDeclaration declaration =
        declared(0x13, "mode:e(none,light,deep)", "", "Sleep mode set to {a0}, restarting");
    EXPECT_EQ(declaration.id, 0x13);
    EXPECT_EQ(declaration.args.fields.size(), 1U);
    EXPECT_TRUE(declaration.reply.empty());
    EXPECT_EQ(declaration.replyTemplate, "Sleep mode set to {a0}, restarting");
}

TEST(TypedDeclarations, RefusesWhatIsNotValid) {
    TypedDeclaration keep = declared(0x13, "", "", "ok");
    EXPECT_FALSE(parseTypedDeclaration(0, "", "", "ok", keep));
    EXPECT_FALSE(parseTypedDeclaration(0x14, "n:x", "", "ok", keep));
    EXPECT_FALSE(parseTypedDeclaration(0x14, "", "n:x", "ok", keep));
    EXPECT_FALSE(parseTypedDeclaration(0x14, "", "n:u", "{1}", keep));
    // Arguments are typed as words, which a group has no form for.
    EXPECT_FALSE(parseTypedDeclaration(0x14, "g:*(a:u)", "", "ok", keep));
    EXPECT_FALSE(parseTypedDeclaration(0x14, "", "", "a|b", keep));
    EXPECT_FALSE(parseTypedDeclaration(0x14, "", "", "a;b", keep));
    EXPECT_FALSE(parseTypedDeclaration(0x14, "", "n:e(a;b)", "", keep));
    EXPECT_EQ(keep.id, 0x13);
}

// --- A typed handler behind a text line ---

TEST(TypedCommand, ATextLineRunsTheTypedHandler) {
    TypedDeclaration declaration =
        declared(0x13, "mode:e(none,light,deep)", "", "Sleep mode set to {a0}, restarting");
    uint32_t seen = 99;
    TypedHandler handler = [&seen](const Fields& in, Fields&) {
        seen = in.u(0);
        return TypedResult();
    };
    CommandResult result = runTypedFromText(declaration, handler, "light");
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.text, "Sleep mode set to light, restarting");
    EXPECT_EQ(seen, 1U);

    // Arguments that do not fit never reach the handler.
    seen = 99;
    for (const char* bad : {"", "heavy", "light now"}) {
        result = runTypedFromText(declaration, handler, bad);
        EXPECT_EQ(result.status, static_cast<uint8_t>(CommandStatus::ARG)) << bad;
        EXPECT_EQ(result.text, "");
    }
    EXPECT_EQ(seen, 99U);
    EXPECT_EQ(runTypedFromText(declaration, TypedHandler(), "light").status,
              static_cast<uint8_t>(CommandStatus::UNKNOWN));
}

TEST(TypedCommand, AnErrorKeepsItsStatusAndItsNumber) {
    TypedDeclaration declaration = declared(0x7A, "seconds:u", "", "ok");
    CommandResult result = runTypedFromText(
        declaration, [](const Fields&, Fields&) { return TypedResult(128, 0x1105); }, "600");
    EXPECT_EQ(result.status, 128);
    EXPECT_EQ(result.text, "4357");
    result = runTypedFromText(
        declaration, [](const Fields&, Fields&) { return TypedResult(129); }, "600");
    EXPECT_EQ(result.status, 129);
    EXPECT_EQ(result.text, "");
    result = runTypedFromText(
        declaration, [](const Fields&, Fields&) { return CommandStatus::BUSY; }, "600");
    EXPECT_EQ(result.status, static_cast<uint8_t>(CommandStatus::BUSY));
}

TEST(TypedCommand, ReplyFieldsThatDoNotFitGiveAnError) {
    TypedDeclaration declaration = declared(0x20, "", "mode:e(a,b)", "{0}");
    TypedHandler handler = [](const Fields&, Fields& out) {
        out.setU(0, 7);
        return TypedResult();
    };
    EXPECT_EQ(runTypedFromText(declaration, handler, "").status,
              static_cast<uint8_t>(CommandStatus::BUSY));
    Fields out;
    out.setU(0, 7);
    Bytes payload = {0xEE};
    EXPECT_FALSE(encodeTypedReply(declaration, TypedResult(), out, payload));
    EXPECT_EQ(payload, (Bytes{0xEE}));
}

TEST(TypedCommand, AnErrorReplyCarriesAnOptionalNumber) {
    TypedDeclaration declaration = firmwareDeclaration("/report.set");
    // ESP_ERR_NVS_NOT_ENOUGH_SPACE is 0x1105.
    Bytes bytes = replyFrame(declaration, TypedResult(128, 0x1105), Fields());
    EXPECT_EQ(bytes, (Bytes{0xA0, 0x80, 0x85, 0x22}));
    Fields in;
    in.setU(0, 600);
    CommandResult result = textOf(declaration, in, bytes);
    EXPECT_EQ(result.status, 128);
    // The command declares that number as an esp_err_t code.
    EXPECT_EQ(result.text, "0x1105");

    bytes = replyFrame(declaration, CommandStatus::ARG, Fields());
    EXPECT_EQ(bytes, (Bytes{0xA0, 0x05}));
    result = textOf(declaration, in, bytes);
    EXPECT_EQ(result.status, static_cast<uint8_t>(CommandStatus::ARG));
    EXPECT_EQ(result.text, "");

    TypedResult out;
    Fields fields;
    const Bytes cut = {0x80};
    EXPECT_FALSE(decodeTypedReply(declaration, 128, cut.data(), cut.size(), out, fields));
}

// --- The number of an error ---

TEST(ErrorDetails, ReadsADeclaration) {
    ErrorDetail detail;
    ASSERT_TRUE(parseErrorDetail(129, "x", detail));
    EXPECT_EQ(detail.code, 129);
    EXPECT_EQ(detail.kind, ErrorDetailKind::HEX_NUMBER);
    ASSERT_TRUE(parseErrorDetail(131, "e(REFUSE_BATTERY,REFUSE_HEAP)", detail));
    EXPECT_EQ(detail.kind, ErrorDetailKind::NAME_OF);
    EXPECT_EQ(detail.names, (std::vector<std::string>{"REFUSE_BATTERY", "REFUSE_HEAP"}));
    for (const char* decimal : {"", "u"}) {
        ASSERT_TRUE(parseErrorDetail(132, decimal, detail)) << decimal;
        EXPECT_EQ(detail.kind, ErrorDetailKind::DECIMAL_NUMBER);
        EXPECT_TRUE(detail.names.empty());
    }
    detail.code = 77;
    for (const char* bad :
         {"X", "xx", "i", "h2", "e", "e()", "e(a,a)", "e(a) b:u", "e(a)?", "s", " x"}) {
        EXPECT_FALSE(parseErrorDetail(128, bad, detail)) << bad;
    }
    EXPECT_EQ(detail.code, 77);
}

TEST(ErrorDetails, RendersTheNumberAsDeclared) {
    std::vector<ErrorDetail> details(2);
    ASSERT_TRUE(parseErrorDetail(129, "x", details[0]));
    ASSERT_TRUE(parseErrorDetail(131, "e(REFUSE_BATTERY,REFUSE_HEAP)", details[1]));
    EXPECT_EQ(errorDetailText(details, 129, 0x1105), "0x1105");
    EXPECT_EQ(errorDetailText(details, 129, 0), "0x0");
    EXPECT_EQ(errorDetailText(details, 129, 0xFFFFFFFF), "0xffffffff");
    EXPECT_EQ(errorDetailText(details, 131, 0), "REFUSE_BATTERY");
    EXPECT_EQ(errorDetailText(details, 131, 1), "REFUSE_HEAP");
    // A number without a name, and a code without a declaration, read as decimal.
    EXPECT_EQ(errorDetailText(details, 131, 2), "2");
    EXPECT_EQ(errorDetailText(details, 132, 8), "8");
    EXPECT_EQ(errorDetailText({}, 129, 4357), "4357");
}

TEST(ErrorDetails, ACommandWithABadDetailIsRefused) {
    CommandCatalogue catalogue;
    EXPECT_FALSE(catalogue.add(spec("/a", Perm::OPEN, "", {{128, "No", "hex"}})));
    EXPECT_FALSE(
        catalogue.add(typed(0x20, spec("/a", Perm::OPEN, "", {{128, "No", "e("}}), "", "", "")));
    EXPECT_TRUE(
        catalogue.add(typed(0x20, spec("/a", Perm::OPEN, "", {{128, "No", "x"}}), "", "", "")));
    TypedDeclaration declaration;
    ASSERT_TRUE(typedDeclarationOf(*catalogue.find("/a"), declaration));
    ASSERT_EQ(declaration.details.size(), 1U);
    EXPECT_EQ(typedReplyText(declaration, Fields(), TypedResult(128, 0x3001), Fields()).text,
              "0x3001");
    // The line of /cmds does not change with the detail.
    EXPECT_EQ(CommandCatalogue::line(*catalogue.find("/a")), "/a|open||128=No|20||=");
}

// --- A reply that must fit one frame ---

TEST(BoundedReply, AReplyThatFitsIsWrittenWhole) {
    TypedDeclaration declaration = declared(0x20, "", "n:u rows:*(a:h2 b:u)", "{0}");
    Fields out;
    out.setU(0, 3);
    for (uint8_t i = 0; i < 3; i++) {
        const uint8_t address[] = {0x10, i};
        Fields row;
        row.setBytes(0, address, 2).setU(1, i);
        out.addRow(row);
    }
    Bytes payload = {0xEE};
    size_t kept = 99;
    ASSERT_TRUE(encodeTypedReplyWithin(declaration, TypedResult(), out, 10, payload, &kept));
    EXPECT_EQ(payload, (Bytes{0xEE, 0x03, 0x10, 0x00, 0x00, 0x10, 0x01, 0x01, 0x10, 0x02, 0x02}));
    EXPECT_EQ(kept, 3U);
}

TEST(BoundedReply, RowsAreDroppedFromTheEndUntilItFits) {
    TypedDeclaration declaration = declared(0x20, "", "n:u rows:*(a:h2 b:u)", "{0}");
    Fields out;
    out.setU(0, 3);
    for (uint8_t i = 0; i < 3; i++) {
        const uint8_t address[] = {0x10, i};
        Fields row;
        row.setBytes(0, address, 2).setU(1, i);
        out.addRow(row);
    }
    Bytes payload;
    size_t kept = 99;
    // One byte short of the third row.
    ASSERT_TRUE(encodeTypedReplyWithin(declaration, TypedResult(), out, 9, payload, &kept));
    EXPECT_EQ(payload, (Bytes{0x03, 0x10, 0x00, 0x00, 0x10, 0x01, 0x01}));
    EXPECT_EQ(kept, 2U);
    // The count field still tells how many rows there were.
    Fields decoded;
    TypedResult result;
    ASSERT_TRUE(decodeTypedReply(declaration, 0, payload.data(), payload.size(), result, decoded));
    EXPECT_EQ(decoded.u(0), 3U);
    EXPECT_EQ(decoded.rowCount(), 2U);

    payload.clear();
    ASSERT_TRUE(encodeTypedReplyWithin(declaration, TypedResult(), out, 1, payload, &kept));
    EXPECT_EQ(payload, (Bytes{0x03}));
    EXPECT_EQ(kept, 0U);
    // The caller may leave the row count out.
    payload.clear();
    EXPECT_TRUE(encodeTypedReplyWithin(declaration, TypedResult(), out, 4, payload));
    EXPECT_EQ(payload, (Bytes{0x03, 0x10, 0x00, 0x00}));
}

TEST(BoundedReply, WhatCannotBeCutIsRefused) {
    TypedDeclaration rows = declared(0x20, "", "n:u rows:*(a:h2 b:u)", "{0}");
    Fields out;
    out.setU(0, 300);
    Bytes payload = {0xEE};
    size_t kept = 99;
    // The fields before the group alone are too long.
    EXPECT_FALSE(encodeTypedReplyWithin(rows, TypedResult(), out, 1, payload, &kept));
    TypedDeclaration text = declared(0x21, "", "name:s", "{0}");
    Fields name;
    name.setText(0, "LM-1A2B");
    EXPECT_FALSE(encodeTypedReplyWithin(text, TypedResult(), name, 6, payload, &kept));
    // Reply fields that do not fit the declaration.
    EXPECT_FALSE(encodeTypedReplyWithin(text, TypedResult(), Fields(), 100, payload, &kept));
    // An error with a number that is too long for the room.
    EXPECT_FALSE(encodeTypedReplyWithin(text, TypedResult(128, 0x1105), Fields(), 1, payload));
    EXPECT_EQ(payload, (Bytes{0xEE}));
    EXPECT_EQ(kept, 99U);
    EXPECT_TRUE(encodeTypedReplyWithin(text, TypedResult(128, 0x1105), Fields(), 2, payload));
    EXPECT_EQ(payload, (Bytes{0xEE, 0x85, 0x22}));
    payload.clear();
    EXPECT_TRUE(encodeTypedReplyWithin(text, TypedResult(), name, 7, payload, &kept));
    EXPECT_EQ(payload, (Bytes{'L', 'M', '-', '1', 'A', '2', 'B'}));
    EXPECT_EQ(kept, 0U);
}

// --- Ids ---

TEST(CommandIds, MatchTheGoldenFile) {
    CommandCatalogue catalogue = firmwareCatalogue();
    std::vector<std::string> pairs;
    for (unsigned id = 1; id <= 255; id++) {
        const CommandSpec* found = catalogue.findById(static_cast<uint8_t>(id));
        if (found == nullptr) continue;
        const uint8_t byte = static_cast<uint8_t>(id);
        pairs.push_back(toHex(&byte, 1) + "=" + found->name);
    }
    std::string text;
    for (const std::string& pair : pairs) text += pair + "\n";
    EXPECT_EQ(text, readGoldenIds());
    EXPECT_EQ(pairs.size(), 28U);
}

TEST(CommandIds, EveryIdSitsInTheBlockOfItsService) {
    CommandCatalogue catalogue = firmwareCatalogue();
    struct Block {
        uint8_t first;
        uint8_t last;
        std::vector<std::string> names;
    };
    // 0x10 node, 0x30 OTA, 0x50 WiFi, 0x60 display, 0x70 mesh and monitor.
    const std::vector<Block> blocks = {
        {0x10,
         0x2F,
         {"/role", "/role.set", "/sleep", "/sleep.set", "/sleep.fraction", "/power", "/sensor",
          "/identify"}},
        {0x30,
         0x4F,
         {"/key", "/maint.open", "/maint.close", "/version", "/ota.status", "/reboot",
          "/maint.wifi"}},
        {0x50, 0x5F, {"/connectWiFi", "/getIP", "/resetWiFiData"}},
        {0x60,
         0x6F,
         {"/displayOn", "/displayOff", "/displayBlink", "/displayClear", "/displayLogo",
          "/displayText"}},
        {0x70, 0x8F, {"/getRT", "/install", "/report", "/report.set"}},
    };
    size_t withId = 0;
    for (const Block& block : blocks) {
        for (const std::string& name : block.names) {
            ASSERT_NE(catalogue.find(name), nullptr) << name;
            EXPECT_GE(catalogue.find(name)->id, block.first) << name;
            EXPECT_LE(catalogue.find(name)->id, block.last) << name;
            withId++;
        }
    }
    EXPECT_EQ(withId, 28U);
}

TEST(CommandIds, CommandsThatNeverTravelHaveNone) {
    CommandCatalogue catalogue = firmwareCatalogue();
    // The local commands, and the two text dumps that a gateway refuses for another node.
    for (const char* name :
         {"/key.set", "/ota.server", "/addSSID", "/addPassword", "/help", "/cmds"}) {
        ASSERT_NE(catalogue.find(name), nullptr) << name;
        EXPECT_EQ(catalogue.find(name)->id, 0) << name;
    }
    EXPECT_EQ(catalogue.size(), 34U);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
