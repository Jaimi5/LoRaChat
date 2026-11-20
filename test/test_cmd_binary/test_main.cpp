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
// The fixture reads limits from NetCore and OtaCore. These includes let PlatformIO find both.
#include "installMode.h"
#include "maintWindowPlan.h"

namespace {

using Bytes = std::vector<uint8_t>;

const CommandTag TAG = {{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};
constexpr uint32_t COUNTER = 0x01020304;

TypedDeclaration declared(uint8_t id, const std::string& args, const std::string& reply,
                          const std::string& replyTemplate) {
    TypedDeclaration declaration;
    EXPECT_TRUE(parseTypedDeclaration(id, args, reply, replyTemplate, declaration));
    return declaration;
}

/** @return the request frame of @p line, as a gateway builds it. */
Bytes requestFrame(const TypedDeclaration& declaration, const std::string& name,
                   const std::string& line, bool isSigned) {
    EXPECT_TRUE(isCanonicalLine(line, name, declaration.args)) << line;
    Fields in;
    EXPECT_TRUE(parseArgs(declaration.args, line.substr(name.size()), in)) << line;
    BinaryCommandFrame frame;
    frame.id = declaration.id;
    frame.isSigned = isSigned;
    frame.counter = COUNTER;
    frame.tag = TAG;
    EXPECT_TRUE(encodeFields(declaration.args, in, frame.payload));
    return frame.encode();
}

/** @return the line a node signs for, built from the bytes of a request frame. */
std::string lineOf(const TypedDeclaration& declaration, const std::string& name,
                   const Bytes& bytes) {
    BinaryCommandFrame frame;
    EXPECT_TRUE(BinaryCommandFrame::decode(bytes.data(), bytes.size(), frame));
    EXPECT_EQ(frame.id, declaration.id);
    Fields in;
    EXPECT_TRUE(decodeFields(declaration.args, frame.payload.data(), frame.payload.size(), in));
    std::string line;
    EXPECT_TRUE(canonicalLine(name, declaration.args, in, line));
    return line;
}

/** @return the reply frame of a typed outcome, as a node builds it. */
Bytes replyFrame(const TypedDeclaration& declaration, const TypedResult& result,
                 const Fields& out) {
    BinaryCommandFrame frame;
    frame.response = true;
    frame.status = result.status;
    EXPECT_TRUE(encodeTypedReply(declaration, result, out, frame.payload));
    return frame.encode();
}

/** @return the text a gateway makes of a reply frame for the request with arguments @p in. */
CommandResult textOf(const TypedDeclaration& declaration, const Fields& in, const Bytes& bytes) {
    BinaryCommandFrame frame;
    EXPECT_TRUE(BinaryCommandFrame::decode(bytes.data(), bytes.size(), frame));
    EXPECT_TRUE(frame.response);
    TypedResult result;
    Fields out;
    EXPECT_TRUE(decodeTypedReply(declaration, frame.status, frame.payload.data(),
                                 frame.payload.size(), result, out));
    return typedReplyText(declaration, in, result, out);
}

/** The reply fields of /sensor as Bme280Sensor::measureInto() writes them. */
Fields sensorReading(bool bme, uint8_t address, int32_t tempCentiC, uint32_t pressurePa,
                     uint32_t rhCentiPercent) {
    Fields out;
    out.setU(0, bme ? 1 : 0).setBytes(1, &address, 1).setI(2, tempCentiC).setU(3, pressurePa);
    if (bme) out.setU(4, rhCentiPercent);
    return out;
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
    frame.counter = COUNTER;
    frame.tag = TAG;
    Bytes bytes = frame.encode();
    EXPECT_EQ(bytes, (Bytes{0x60, 0x13, 0x01, 0x04, 0x03, 0x02, 0x01, 0x11, 0x22, 0x33, 0x44, 0x55,
                            0x66, 0x77, 0x88}));

    BinaryCommandFrame out;
    ASSERT_TRUE(BinaryCommandFrame::decode(bytes.data(), bytes.size(), out));
    EXPECT_TRUE(out.isSigned);
    EXPECT_EQ(out.id, 0x13);
    EXPECT_EQ(out.payload, (Bytes{0x01}));
    EXPECT_EQ(out.counter, COUNTER);
    EXPECT_EQ(out.tag, TAG);
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
    EXPECT_EQ(result.text, "4357");

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

// --- The examples of the design, as whole frames ---

TEST(GoldenFrames, Version) {
    TypedDeclaration declaration = firmwareDeclaration("/version");
    Bytes request = requestFrame(declaration, "/version", "/version", false);
    EXPECT_EQ(request, (Bytes{0x20, 0x34}));
    EXPECT_EQ(lineOf(declaration, "/version", request), "/version");

    // 0.1.2 in slot app0, valid, bootloader 20f487, board tbeam.
    Fields out;
    const uint8_t bootloader[] = {0x20, 0xF4, 0x87};
    out.setVersion(0, 0, 1, 2).setU(1, 0).setU(2, 0).setBytes(3, bootloader, 3).setU(4, 0);
    Bytes reply = replyFrame(declaration, TypedResult(), out);
    EXPECT_EQ(reply, (Bytes{0xA0, 0x00, 0x00, 0x01, 0x02, 0x01, 0x00, 0x20, 0xF4, 0x87, 0x01}));
    EXPECT_EQ(reply.size(), 11U);
    EXPECT_EQ(textOf(declaration, Fields(), reply).text, "0.1.2 app0 VALID bl=20f487 env=tbeam");

    // A new image on the other slot of a debug build.
    Fields pending;
    pending.setVersion(0, 1, 20, 255).setU(1, 1).setU(2, 1).setBytes(3, bootloader, 3).setU(4, 1);
    reply = replyFrame(declaration, TypedResult(), pending);
    EXPECT_EQ(reply, (Bytes{0xA0, 0x00, 0x01, 0x14, 0xFF, 0x02, 0x01, 0x20, 0xF4, 0x87, 0x02}));
    EXPECT_EQ(textOf(declaration, Fields(), reply).text,
              "1.20.255 app1 PENDING_VERIFY bl=20f487 env=tbeam-debug");

    // A slot and a board this firmware has no name for.
    Fields unknown;
    unknown.setVersion(0, 0, 1, 2).setU(2, 0).setBytes(3, bootloader, 3);
    reply = replyFrame(declaration, TypedResult(), unknown);
    EXPECT_EQ(reply, (Bytes{0xA0, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x20, 0xF4, 0x87}));
    EXPECT_EQ(textOf(declaration, Fields(), reply).text, "0.1.2 VALID bl=20f487");
}

TEST(GoldenFrames, SleepSetLight) {
    // The command keeps its text handler for now, so the declaration is the one of the design.
    TypedDeclaration declaration =
        declared(0x13, "mode:e(none,light,deep)", "", "Sleep mode set to {a0}, restarting");
    Bytes request = requestFrame(declaration, "/sleep.set", "/sleep.set light", true);
    EXPECT_EQ(request, (Bytes{0x60, 0x13, 0x01, 0x04, 0x03, 0x02, 0x01, 0x11, 0x22, 0x33, 0x44,
                              0x55, 0x66, 0x77, 0x88}));
    EXPECT_EQ(request.size(), 15U);
    EXPECT_EQ(lineOf(declaration, "/sleep.set", request), "/sleep.set light");

    Bytes reply = replyFrame(declaration, TypedResult(), Fields());
    EXPECT_EQ(reply, (Bytes{0xA0, 0x00}));
    Fields in;
    in.setU(0, 1);
    EXPECT_EQ(textOf(declaration, in, reply).text, "Sleep mode set to light, restarting");
}

TEST(GoldenFrames, Sensor) {
    TypedDeclaration declaration = firmwareDeclaration("/sensor");
    Bytes request = requestFrame(declaration, "/sensor", "/sensor", false);
    EXPECT_EQ(request, (Bytes{0x20, 0x18}));
    EXPECT_EQ(lineOf(declaration, "/sensor", request), "/sensor");

    // BMP280 at 0x76: 21.50 C and 1013.25 hPa.
    Bytes reply =
        replyFrame(declaration, TypedResult(), sensorReading(false, 0x76, 2150, 101325, 0));
    EXPECT_EQ(reply, (Bytes{0xA0, 0x00, 0x00, 0x76, 0xCC, 0x21, 0xCD, 0x97, 0x06}));
    EXPECT_EQ(reply.size(), 9U);
    EXPECT_EQ(textOf(declaration, Fields(), reply).text, "BMP280 at 0x76: 21.50 C 1013.25 hPa");

    // BME280 at 0x77 with 45.50 %RH.
    reply = replyFrame(declaration, TypedResult(), sensorReading(true, 0x77, 2150, 101325, 4550));
    EXPECT_EQ(reply, (Bytes{0xA0, 0x00, 0x01, 0x77, 0xCC, 0x21, 0xCD, 0x97, 0x06, 0xC7, 0x23}));
    EXPECT_EQ(reply.size(), 11U);
    EXPECT_EQ(textOf(declaration, Fields(), reply).text,
              "BME280 at 0x77: 21.50 C 45.50 %RH 1013.25 hPa");

    // An error of the command has no fields.
    EXPECT_EQ(replyFrame(declaration, TypedResult(128), Fields()), (Bytes{0xA0, 0x80}));
}

// The serial console and MQTT show what the text handler printed before the command was typed.
TEST(GoldenFrames, SensorTextIsTheOneOfTheTextCommand) {
    TypedDeclaration declaration = firmwareDeclaration("/sensor");
    struct Case {
        Fields out;
        const char* text;
    };
    Fields stale = sensorReading(false, 0x76, 2150, 101325, 0);
    stale.setU(5, 75);
    Fields staleNow = sensorReading(true, 0x77, 2150, 101325, 4550);
    staleNow.setU(5, 0);
    for (const Case& c : std::vector<Case>{
             {sensorReading(false, 0x76, 2150, 101325, 0), "BMP280 at 0x76: 21.50 C 1013.25 hPa"},
             {sensorReading(true, 0x77, 2150, 101325, 4550),
              "BME280 at 0x77: 21.50 C 45.50 %RH 1013.25 hPa"},
             {sensorReading(true, 0x76, -5, 30000, 0),
              "BME280 at 0x76: -0.05 C 0.00 %RH 300.00 hPa"},
             {sensorReading(true, 0x76, -1234, 110007, 10000),
              "BME280 at 0x76: -12.34 C 100.00 %RH 1100.07 hPa"},
             {sensorReading(false, 0x77, 0, 99999, 0), "BMP280 at 0x77: 0.00 C 999.99 hPa"},
             {stale,
              "BMP280 at 0x76: 21.50 C 1013.25 hPa, read 75 s ago, the sensor does not answer now"},
             {staleNow, "BME280 at 0x77: 21.50 C 45.50 %RH 1013.25 hPa, read 0 s ago, the sensor "
                        "does not answer now"},
         }) {
        const Fields fields = c.out;
        CommandResult direct = runTypedFromText(
            declaration,
            [&fields](const Fields&, Fields& out) {
                out = fields;
                return TypedResult();
            },
            "");
        EXPECT_TRUE(direct.ok());
        EXPECT_EQ(direct.text, c.text);
        // The same text after the trip over LoRa.
        EXPECT_EQ(
            textOf(declaration, Fields(), replyFrame(declaration, TypedResult(), fields)).text,
            c.text);
    }
    // /sensor takes no argument.
    EXPECT_EQ(runTypedFromText(
                  declaration, [](const Fields&, Fields&) { return TypedResult(); }, "now")
                  .status,
              static_cast<uint8_t>(CommandStatus::ARG));
    CommandResult error =
        runTypedFromText(declaration, [](const Fields&, Fields&) { return TypedResult(129); }, "");
    EXPECT_EQ(error.status, 129);
    EXPECT_EQ(error.text, "");
}

TEST(GoldenFrames, ReportSet600) {
    TypedDeclaration declaration = firmwareDeclaration("/report.set");
    Bytes request = requestFrame(declaration, "/report.set", "/report.set 600", true);
    EXPECT_EQ(request, (Bytes{0x60, 0x7A, 0xD8, 0x04, 0x04, 0x03, 0x02, 0x01, 0x11, 0x22, 0x33,
                              0x44, 0x55, 0x66, 0x77, 0x88}));
    EXPECT_EQ(request.size(), 16U);
    EXPECT_EQ(lineOf(declaration, "/report.set", request), "/report.set 600");
    EXPECT_FALSE(isCanonicalLine("/report.set 0600", "/report.set", declaration.args));

    Bytes reply = replyFrame(declaration, TypedResult(), Fields());
    EXPECT_EQ(reply, (Bytes{0xA0, 0x00}));
    Fields in;
    in.setU(0, 600);
    // The text of the text command.
    EXPECT_EQ(textOf(declaration, in, reply).text, "Report interval set to 600 s");

    // 0 removes the stored interval. The reply then names the default of the node.
    Fields toDefault;
    toDefault.setU(0, 300);
    reply = replyFrame(declaration, TypedResult(), toDefault);
    EXPECT_EQ(reply, (Bytes{0xA0, 0x00, 0xAD, 0x02}));
    in.setU(0, 0);
    EXPECT_EQ(textOf(declaration, in, reply).text,
              "Report interval set to 0 s (the default, 300 s)");
}

TEST(GoldenFrames, Sleep) {
    // The command keeps its text handler for now. A request without arguments is two bytes.
    TypedDeclaration declaration = declared(0x12, "", "", "");
    Bytes request = requestFrame(declaration, "/sleep", "/sleep", false);
    EXPECT_EQ(request, (Bytes{0x20, 0x12}));
    EXPECT_EQ(lineOf(declaration, "/sleep", request), "/sleep");
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
    EXPECT_EQ(pairs.size(), 3U);
}

TEST(CommandIds, EveryIdSitsInTheBlockOfItsService) {
    CommandCatalogue catalogue = firmwareCatalogue();
    struct Case {
        const char* name;
        uint8_t first;
        uint8_t last;
    };
    // 0x10 node, 0x30 OTA, 0x50 WiFi, 0x60 display, 0x70 mesh and monitor.
    for (const Case& c : std::vector<Case>{
             {"/sensor", 0x10, 0x2F}, {"/version", 0x30, 0x4F}, {"/report.set", 0x70, 0x8F}}) {
        ASSERT_NE(catalogue.find(c.name), nullptr);
        EXPECT_GE(catalogue.find(c.name)->id, c.first) << c.name;
        EXPECT_LE(catalogue.find(c.name)->id, c.last) << c.name;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
