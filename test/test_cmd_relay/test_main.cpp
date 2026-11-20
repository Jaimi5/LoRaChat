#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "cmdAuth.h"
#include "cmdCatalogue.h"
#include "cmdFrame.h"
#include "cmdRelay.h"
#include "cmdStatus.h"
#include "cmdText.h"
#include "cmdVectors.h"
#include "deploymentKey.h"
#include "firmwareCatalogue.h"
// The fixture reads limits from NetCore and OtaCore. These includes let PlatformIO find both.
#include "installMode.h"
#include "maintWindowPlan.h"

/**
 * A command from the MQTT text at the gateway to the text of its reply, over the bytes that
 * travel on LoRa. The gateway and the node are the functions of cmdRelay.h, which the command
 * router calls, with the catalogue of the firmware and handlers that write fixed fields.
 */
namespace {

using Bytes = std::vector<uint8_t>;

/** Most bytes of a command frame at SF9. */
constexpr size_t FRAME_MAX_BYTES = 102;
constexpr uint16_t NODE = 0x7680;

KeyedHmac vectorKey() {
    return [](const uint8_t* data, size_t size) {
        return hmacSha256(CMD_VECTOR_KEY, sizeof(CMD_VECTOR_KEY), data, size);
    };
}

/** The gateway: its catalogue and the nodes that announced binary commands. */
struct Gateway {
    CommandCatalogue catalogue = firmwareCatalogue();
    BinaryCapableNodes binaryNodes;

    /** @return what the gateway does with the MQTT text "<dst> <request id> <line>". */
    ForwardPlan forward(const std::string& mqttText) {
        MqttCommand command;
        EXPECT_TRUE(parseMqttCommand(mqttText, command)) << mqttText;
        requestId = command.requestId;
        std::string name;
        std::string args;
        splitCommand(command.command.line, name, args);
        return planForward(command.command, args, catalogue.find(name),
                           binaryNodes.has(command.dst), FRAME_MAX_BYTES);
    }

    /** @return the payload of cmd-resp for the reply frame @p bytes to the command of @p plan. */
    std::string publish(const ForwardPlan& plan, const Bytes& bytes) {
        return formatReplyPayload(requestId, read(plan, bytes));
    }

    CommandResult read(const ForwardPlan& plan, const Bytes& bytes) {
        if (!isBinaryCommandFrame(bytes.data(), bytes.size())) {
            CommandFrame text;
            EXPECT_TRUE(CommandFrame::decode(bytes.data(), bytes.size(), text));
            return CommandResult(text.status, text.line);
        }
        BinaryCommandFrame reply;
        EXPECT_TRUE(BinaryCommandFrame::decode(bytes.data(), bytes.size(), reply));
        EXPECT_TRUE(reply.response);
        TypedDeclaration declaration;
        const CommandSpec* spec = catalogue.find(plan.name);
        const bool declared = spec != nullptr && typedDeclarationOf(*spec, declaration);
        return renderBinaryReply(reply, declared ? &declaration : nullptr, plan.args);
    }

    uint8_t requestId = 0;
};

/** A node: its key, its replay counter and a handler that writes the fields it is given. */
struct Node {
    CommandCatalogue catalogue = firmwareCatalogue();
    uint16_t address = NODE;
    bool hasKey = true;
    bool storageWorks = true;
    bool counterLoads = true;
    CommandCounter counter;
    std::vector<uint32_t> stored;
    int runs = 0;
    Fields seen;
    Fields out;
    TypedResult result;

    /** @return the reply frame to the request frame @p bytes. */
    Bytes serve(const Bytes& bytes, size_t maxFrame = FRAME_MAX_BYTES) {
        BinaryCommandFrame request;
        EXPECT_TRUE(BinaryCommandFrame::decode(bytes.data(), bytes.size(), request));
        ServedCommand served;
        const CommandSpec* spec = catalogue.findById(request.id);
        const bool known = spec != nullptr && typedDeclarationOf(*spec, served.declaration);
        if (known) {
            served.name = spec->name;
            served.perm = spec->perm;
            served.handler = [this](const Fields& in, Fields& written) {
                runs++;
                seen = in;
                written = out;
                return result;
            };
        }
        NodeAuth auth;
        auth.address = address;
        if (hasKey) auth.hmac = vectorKey();
        auth.counter = [this]() { return counterLoads ? &counter : nullptr; };
        auth.store = [this](uint32_t value) {
            if (storageWorks) stored.push_back(value);
            return storageWorks;
        };
        return serveBinaryCommand(request, known ? &served : nullptr, Origin::LORA, auth, maxFrame)
            .encode();
    }

    /** The node restarts: the counter comes back from storage, the reply does not. */
    void restart() {
        counter = stored.empty() ? CommandCounter() : CommandCounter(stored.back(), true);
    }
};

/**
 * What firmware from before the binary frames does with a frame: it reads every frame as text.
 * @return its reply frame, or nothing if it drops the frame.
 */
Bytes oldNodeReply(const Bytes& bytes) {
    CommandFrame request;
    if (!CommandFrame::decode(bytes.data(), bytes.size(), request)) return {};
    CommandFrame reply;
    reply.response = true;
    // The bytes behind the flags are no command name, and no signature covers them.
    reply.status =
        static_cast<uint8_t>(request.isSigned ? CommandStatus::AUTH : CommandStatus::UNKNOWN);
    return reply.encode();
}

/** @return "<dst> <request id> <line> #<counter>.<tag>" signed with the vector key. */
std::string signedMqtt(uint8_t requestId, uint32_t counter, const std::string& line,
                       uint16_t dst = NODE) {
    const CommandTag tag = commandTag(vectorKey(), dst, counter, line);
    char target[8];
    snprintf(target, sizeof(target), "%04X ", static_cast<unsigned>(dst));
    return target + std::to_string(requestId) + " " + line + formatSignature(counter, tag);
}

Fields sleepFields() {
    Fields out;
    out.setU(0, 1).setU(1, 1).setU(2, 20).setU(4, 12).setU(5, 340).setU(6, 0).setU(7, 0);
    out.setU(8, 3);
    return out;
}

}  // namespace

// --- The nodes a gateway knows ---

TEST(BinaryNodes, ANodeIsATextNodeUntilItsReportSaysOtherwise) {
    BinaryCapableNodes nodes;
    EXPECT_FALSE(nodes.has(NODE));
    nodes.note(NODE, true);
    nodes.note(NODE, true);
    EXPECT_TRUE(nodes.has(NODE));
    EXPECT_EQ(nodes.size(), 1U);
    EXPECT_FALSE(nodes.has(0x1A2B));
    // A report without the flag: the node runs older firmware again.
    nodes.note(NODE, false);
    EXPECT_FALSE(nodes.has(NODE));
    nodes.note(0x1A2B, false);
    EXPECT_EQ(nodes.size(), 0U);
}

TEST(BinaryNodes, AFullTableForgetsTheNodeItLearnedFirst) {
    BinaryCapableNodes nodes(3);
    for (uint16_t node = 1; node <= 4; node++) nodes.note(node, true);
    EXPECT_EQ(nodes.size(), 3U);
    EXPECT_FALSE(nodes.has(1));
    EXPECT_TRUE(nodes.has(2));
    EXPECT_TRUE(nodes.has(4));
    BinaryCapableNodes none(0);
    none.note(1, true);
    EXPECT_FALSE(none.has(1));
}

// --- Text in at the gateway, bytes on air, text out ---

TEST(EndToEnd, Version) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    Node node;
    const uint8_t bootloader[] = {0x20, 0xF4, 0x87};
    const uint8_t commit[] = {0x2B, 0xFD, 0xEE, 0xB1};
    node.out.setVersion(0, 0, 1, 2).setU(1, 0).setU(2, 0).setBytes(3, bootloader, 3).setU(4, 0);
    node.out.setBytes(5, commit, 4);

    const ForwardPlan plan = gateway.forward("7680 12 /version");
    ASSERT_TRUE(plan.send());
    EXPECT_TRUE(plan.binary);
    EXPECT_EQ(plan.frame, (Bytes{0x20, 0x34}));
    const Bytes reply = node.serve(plan.frame);
    EXPECT_EQ(reply, (Bytes{0xA0, 0x00, 0x00, 0x01, 0x02, 0x01, 0x00, 0x20, 0xF4, 0x87, 0x01, 0x2B,
                            0xFD, 0xEE, 0xB1}));
    EXPECT_EQ(gateway.publish(plan, reply), "12 0.1.2+g2bfdeeb1 app0 VALID bl=20f487 env=tbeam");
    EXPECT_EQ(node.runs, 1);
}

TEST(EndToEnd, Sleep) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    Node node;
    node.out = sleepFields();
    const ForwardPlan plan = gateway.forward("7680 13 /sleep");
    ASSERT_TRUE(plan.send());
    EXPECT_EQ(plan.frame, (Bytes{0x20, 0x12}));
    const Bytes reply = node.serve(plan.frame);
    EXPECT_EQ(reply,
              (Bytes{0xA0, 0x00, 0x01, 0x01, 0x15, 0x00, 0x0C, 0xD4, 0x02, 0x00, 0x00, 0x04}));
    // The whole text, which a text frame cuts at 100 characters.
    const std::string text = gateway.publish(plan, reply);
    EXPECT_EQ(text, "13 sleep light (stored), fraction 20 % (stored), light 12 x 340 s, deep 0 x 0 "
                    "s since power-on, 3 light sleeps ended by the power key");
    EXPECT_GT(text.size(), 100U);
}

TEST(EndToEnd, Sensor) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    Node node;
    const uint8_t address = 0x77;
    node.out.setU(0, 1).setBytes(1, &address, 1).setI(2, 2150).setU(3, 101325).setU(4, 4550);
    const ForwardPlan plan = gateway.forward("7680 14 /sensor");
    ASSERT_TRUE(plan.send());
    EXPECT_EQ(plan.frame, (Bytes{0x20, 0x18}));
    Bytes reply = node.serve(plan.frame);
    EXPECT_EQ(reply, (Bytes{0xA0, 0x00, 0x01, 0x77, 0xCC, 0x21, 0xCD, 0x97, 0x06, 0xC7, 0x23}));
    EXPECT_EQ(gateway.publish(plan, reply), "14 BME280 at 0x77: 21.50 C 45.50 %RH 1013.25 hPa");
    // The node has no sensor.
    node.result = TypedResult(128);
    reply = node.serve(plan.frame);
    EXPECT_EQ(reply, (Bytes{0xA0, 0x80}));
    EXPECT_EQ(gateway.publish(plan, reply), "14 E128");
}

TEST(EndToEnd, ReportSet600Signed) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    Node node;
    node.out.setU(0, 600);
    // The line scripts/ota_tools/lmcmd.py signs for node 7680 with counter 0x65000001.
    const std::string mqtt = signedMqtt(15, 0x65000001, "/report.set 600");
    EXPECT_EQ(mqtt, "7680 15 /report.set 600 #65000001.a2b5ad451bf494bc");
    const ForwardPlan plan = gateway.forward(mqtt);
    ASSERT_TRUE(plan.send());
    EXPECT_EQ(plan.frame, (Bytes{0x60, 0x7A, 0xD8, 0x04, 0x01, 0x00, 0x00, 0x65, 0xA2, 0xB5, 0xAD,
                                 0x45, 0x1B, 0xF4, 0x94, 0xBC}));
    const Bytes reply = node.serve(plan.frame);
    EXPECT_EQ(reply, (Bytes{0xA0, 0x00, 0xD9, 0x04}));
    EXPECT_EQ(gateway.publish(plan, reply), "15 Report interval set to 600 s");
    EXPECT_EQ(node.seen.u(0), 600U);
    EXPECT_EQ(node.stored, (std::vector<uint32_t>{0x65000001}));

    // The error of the command comes back with its number in hex.
    Node full;
    full.result = TypedResult(128, 0x1105);
    const Bytes error = full.serve(plan.frame);
    EXPECT_EQ(error, (Bytes{0xA0, 0x80, 0x85, 0x22}));
    EXPECT_EQ(gateway.publish(plan, error), "15 E128 0x1105");
}

TEST(EndToEnd, TheVectorsOfThePythonToolPassAsBinaryFrames) {
    Gateway gateway;
    Node node;
    for (const CmdTagVector& vector : CMD_TAG_VECTORS) {
        // Each vector is "@<dst> <line> #<counter>.<tag>".
        CommandLine line;
        ASSERT_TRUE(parseCommandLine(vector.serial, line));
        std::string name;
        std::string args;
        splitCommand(line.line, name, args);
        const ForwardPlan plan =
            planForward(line, args, gateway.catalogue.find(name), true, FRAME_MAX_BYTES);
        ASSERT_TRUE(plan.send()) << vector.serial;
        EXPECT_TRUE(plan.binary);
        Node target;
        target.address = vector.dst;
        target.out = Fields();
        if (name == "/version") continue;
        BinaryCommandFrame reply;
        const Bytes bytes = target.serve(plan.frame);
        ASSERT_TRUE(BinaryCommandFrame::decode(bytes.data(), bytes.size(), reply));
        // The signature held: the handler ran. /maint.open needs reply fields the fake lacks.
        EXPECT_EQ(target.runs, 1) << vector.serial;
        EXPECT_NE(reply.status, static_cast<uint8_t>(CommandStatus::AUTH)) << vector.serial;
    }
    // The sealed WiFi line of the tool: the bytes travel raw and the line still verifies.
    const std::string wifi = signedMqtt(16, CMD_SEAL_COUNTER, CMD_WIFI_LINE, CMD_SEAL_DST);
    gateway.binaryNodes.note(CMD_SEAL_DST, true);
    const ForwardPlan plan = gateway.forward(wifi);
    ASSERT_TRUE(plan.send());
    // 20 sealed bytes in place of 40 hex digits.
    EXPECT_EQ(plan.frame.size(), 2U + 20U + 12U);
    node.address = CMD_SEAL_DST;
    node.out.setText(1, CMD_WIFI_SSID);
    const Bytes reply = node.serve(plan.frame);
    EXPECT_EQ(gateway.publish(plan, reply), "16 WiFi credentials stored for BenchNet");
    std::vector<uint8_t> sealed;
    ASSERT_TRUE(fromHex(std::string(CMD_WIFI_LINE).substr(12), sealed));
    EXPECT_EQ(node.seen.bytes(0), sealed);
}

TEST(EndToEnd, AReplyWithRoutesIsCutToTheFrame) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    Node node;
    node.out.setU(0, 20);
    for (uint8_t i = 0; i < 20; i++) {
        const uint8_t address[] = {0x10, i};
        Fields row;
        row.setBytes(0, address, 2).setBytes(1, address, 2).setU(2, 255).setU(3, 1);
        row.setU(4, 127).setI(5, -3200);
        node.out.addRow(row);
    }
    const ForwardPlan plan = gateway.forward("7680 17 /getRT");
    ASSERT_TRUE(plan.send());
    const Bytes reply = node.serve(plan.frame);
    EXPECT_EQ(reply.size(), FRAME_MAX_BYTES);
    const std::string text = gateway.publish(plan, reply);
    EXPECT_EQ(text.compare(0, 29, "17 Routing table, 20 routes: "), 0) << text;
    EXPECT_EQ(text.substr(text.size() - 53),
              "1008 (hops:1 lq:255 rssi:-127 snr:-32.00) - Via: 1008");
    // A reply without a group that does not fit is an error of the node.
    const ForwardPlan version = gateway.forward("7680 18 /version");
    node.out = Fields();
    const uint8_t bytes[] = {1, 2, 3, 4};
    node.out.setVersion(0, 0, 1, 2).setU(2, 0).setBytes(3, bytes, 3).setBytes(5, bytes, 4);
    EXPECT_EQ(node.serve(version.frame, 8), (Bytes{0xA0, 0x0B}));
    // Reply fields that do not fit the declaration.
    node.out = Fields();
    EXPECT_EQ(node.serve(version.frame), (Bytes{0xA0, 0x0B}));
}

// --- What the gateway answers itself ---

TEST(Gateway, HelpAndCmdsStayOnTheNode) {
    Gateway gateway;
    for (bool binary : {false, true}) {
        gateway.binaryNodes.note(NODE, binary);
        for (const char* text :
             {"7680 1 /help", "7680 1 /cmds", "7680 1 /cmds 2", "7680 1 /HELP"}) {
            const ForwardPlan plan = gateway.forward(text);
            EXPECT_FALSE(plan.send()) << text;
            EXPECT_EQ(plan.status, CommandStatus::NOT_REMOTE) << text;
            EXPECT_TRUE(plan.frame.empty());
            EXPECT_EQ(formatReplyPayload(1, CommandResult(plan.status, plan.argument)), "1 E13");
        }
    }
}

TEST(Gateway, ABinaryNodeGetsOnlyWhatItsSchemaReads) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    struct Case {
        const char* text;
        CommandStatus status;
    };
    for (const Case& c : std::vector<Case>{
             {"7680 1 /nosuch", CommandStatus::UNKNOWN},
             // Local commands have no id.
             {"7680 1 /key.set 00", CommandStatus::PERM},
             {"7680 1 /addSSID home", CommandStatus::PERM},
             {"7680 1 /version now", CommandStatus::ARG},
             {"7680 1 /identify 1x", CommandStatus::ARG},
             {"7680 1 /displayText", CommandStatus::ARG},
             // The address form of the display commands is for text lines.
             {"7680 1 /displayOn 1A2B", CommandStatus::ARG},
             {"7680 1 /report.set default", CommandStatus::ARG},
         }) {
        const ForwardPlan plan = gateway.forward(c.text);
        EXPECT_EQ(plan.status, c.status) << c.text;
        EXPECT_TRUE(plan.frame.empty()) << c.text;
    }
    // A line without a signature may be typed loosely. The frame is the same.
    for (const char* text :
         {"7680 1 /identify 30", "7680 1 /identify 030", "7680 1 /IDENTIFY 30"}) {
        const ForwardPlan plan = gateway.forward(text);
        ASSERT_TRUE(plan.send()) << text;
        EXPECT_EQ(plan.frame, (Bytes{0x20, 0x1A, 0x1F})) << text;
    }
    // /displayText with an address in front shows the address: it is text.
    const ForwardPlan text = gateway.forward("7680 1 /displayText 1A2B hi");
    ASSERT_TRUE(text.send());
    EXPECT_EQ(text.frame, (Bytes{0x20, 0x65, '1', 'A', '2', 'B', ' ', 'h', 'i'}));
}

TEST(Gateway, ASignedLineMustBeCanonicalForEveryNode) {
    Gateway gateway;
    Node node;
    for (bool binary : {false, true}) {
        gateway.binaryNodes.note(NODE, binary);
        for (const char* line :
             {"/report.set 0600", "/report.set default", "/role.set Gateway", "/sleep.fraction 050",
              "/REPORT.SET 600", "/reboot now", "/maint.wifi DEADBEEF"}) {
            const ForwardPlan plan = gateway.forward(signedMqtt(1, 7, line));
            EXPECT_EQ(plan.status, CommandStatus::ARG) << line;
            EXPECT_TRUE(plan.frame.empty());
        }
        for (const char* line :
             {"/report.set 600", "/report.set 0", "/role.set gateway", "/sleep.fraction 50",
              "/reboot", "/install", "/install 0", "/maint.open 600 ap", "/maint.wifi deadbeef"}) {
            const ForwardPlan plan = gateway.forward(signedMqtt(1, 7, line));
            EXPECT_TRUE(plan.send()) << line;
            EXPECT_EQ(plan.binary, binary) << line;
        }
    }
    EXPECT_EQ(node.runs, 0);
}

TEST(Gateway, AWordThatStandsForAValueIsReplacedOnlyWithoutASignature) {
    const CommandCatalogue catalogue = firmwareCatalogue();
    const CommandSpec* spec = catalogue.find("/report.set");
    ASSERT_NE(spec, nullptr);
    // The router hands over what the command made of the arguments of an unsigned line.
    CommandLine unsignedLine;
    unsignedLine.line = "/report.set default";
    ForwardPlan plan = planForward(unsignedLine, "0", spec, true, FRAME_MAX_BYTES);
    ASSERT_TRUE(plan.send());
    EXPECT_EQ(plan.frame, (Bytes{0x20, 0x7A, 0x00}));
    EXPECT_EQ(plan.args.u(0), 0U);
    // A text node gets the line as it was typed, which its text path reads.
    plan = planForward(unsignedLine, "0", spec, false, FRAME_MAX_BYTES);
    ASSERT_TRUE(plan.send());
    EXPECT_FALSE(plan.binary);
    EXPECT_EQ(std::string(plan.frame.begin() + 1, plan.frame.end()), "/report.set default");
    // The signature of a signed line covers the word, so nothing can replace it.
    CommandLine signedLine = unsignedLine;
    signedLine.isSigned = true;
    EXPECT_EQ(planForward(signedLine, "default", spec, true, FRAME_MAX_BYTES).status,
              CommandStatus::ARG);
    EXPECT_EQ(planForward(signedLine, "default", spec, false, FRAME_MAX_BYTES).status,
              CommandStatus::ARG);
}

TEST(Gateway, AFrameThatIsTooLongIsRefusedWithItsLimit) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    // 100 bytes of text fit behind the flags and the id.
    ForwardPlan plan = gateway.forward("7680 1 /displayText " + std::string(100, 'x'));
    ASSERT_TRUE(plan.send());
    EXPECT_EQ(plan.frame.size(), FRAME_MAX_BYTES);
    plan = gateway.forward("7680 1 /displayText " + std::string(101, 'x'));
    EXPECT_EQ(plan.status, CommandStatus::TOO_LONG);
    EXPECT_EQ(plan.argument, "100");
    EXPECT_TRUE(plan.frame.empty());
    // The text frame keeps its own limit: the bytes of the line.
    gateway.binaryNodes.note(NODE, false);
    plan = gateway.forward("7680 1 /displayText " + std::string(88, 'x'));
    ASSERT_TRUE(plan.send());
    EXPECT_EQ(plan.frame.size(), FRAME_MAX_BYTES);
    plan = gateway.forward("7680 1 /displayText " + std::string(89, 'x'));
    EXPECT_EQ(plan.status, CommandStatus::TOO_LONG);
    EXPECT_EQ(plan.argument, "101");
}

// --- Mixed versions ---

TEST(MixedVersions, ANodeThatNeverReportedGetsTextAsBefore) {
    Gateway gateway;
    // No report yet, or the gateway restarted and lost its table.
    const ForwardPlan plan = gateway.forward("7680 12 /version");
    ASSERT_TRUE(plan.send());
    EXPECT_FALSE(plan.binary);
    EXPECT_EQ(plan.frame, (Bytes{0x00, '/', 'v', 'e', 'r', 's', 'i', 'o', 'n'}));
    // The signed text frame of today, byte for byte.
    const ForwardPlan signedPlan = gateway.forward(signedMqtt(15, 0x65000001, "/report.set 600"));
    ASSERT_TRUE(signedPlan.send());
    CommandFrame expected;
    expected.line = "/report.set 600";
    expected.isSigned = true;
    expected.counter = 0x65000001;
    expected.tag = commandTag(vectorKey(), NODE, 0x65000001, "/report.set 600");
    EXPECT_EQ(signedPlan.frame, expected.encode());
    EXPECT_EQ(signedPlan.frame.size(), 28U);
    // A name the gateway does not know still goes to a text node.
    const ForwardPlan unknown = gateway.forward("7680 12 /futureCommand 1");
    ASSERT_TRUE(unknown.send());
    EXPECT_FALSE(unknown.binary);
    // Its text reply is delivered as it comes.
    CommandFrame reply;
    reply.response = true;
    reply.line = "0.1.2+g2bfdeeb app0 VALID bl=20f487 env=tbeam node=7680";
    EXPECT_EQ(gateway.publish(plan, reply.encode()),
              "12 0.1.2+g2bfdeeb app0 VALID bl=20f487 env=tbeam node=7680");
}

TEST(MixedVersions, AnOldNodeAnswersABinaryFrameWithATextError) {
    Gateway gateway;
    // The table is wrong: the node went back to older firmware after its last report.
    gateway.binaryNodes.note(NODE, true);
    ForwardPlan plan = gateway.forward("7680 12 /version");
    ASSERT_TRUE(plan.binary);
    Bytes reply = oldNodeReply(plan.frame);
    EXPECT_EQ(reply, (Bytes{0x80, 0x01}));
    EXPECT_FALSE(isBinaryCommandFrame(reply.data(), reply.size()));
    EXPECT_EQ(gateway.publish(plan, reply), "12 E1");
    // A signed frame fails its signature there.
    plan = gateway.forward(signedMqtt(15, 0x65000001, "/report.set 600"));
    reply = oldNodeReply(plan.frame);
    EXPECT_EQ(reply, (Bytes{0x80, 0x03}));
    EXPECT_EQ(gateway.publish(plan, reply), "15 E3");
    // A frame with a zero byte is no text frame for it. It is dropped and the gateway times out.
    plan = gateway.forward(signedMqtt(16, 0x65000002, "/report.set 0"));
    ASSERT_TRUE(plan.send());
    EXPECT_TRUE(oldNodeReply(plan.frame).empty());
}

TEST(MixedVersions, ANewNodeTellsTheTwoFormsApartByBitFive) {
    // An old gateway sends text frames. The node reads them on its text path.
    CommandFrame text;
    text.line = "/report.set 600";
    text.isSigned = true;
    text.counter = 0x65000001;
    const Bytes textBytes = text.encode();
    EXPECT_FALSE(isBinaryCommandFrame(textBytes.data(), textBytes.size()));
    CommandFrame decoded;
    ASSERT_TRUE(CommandFrame::decode(textBytes.data(), textBytes.size(), decoded));
    EXPECT_EQ(decoded.line, "/report.set 600");
    // Every text frame starts with flags that have bit 5 clear.
    for (const char* line : {"/version", "/help", "/displayText hi"}) {
        CommandFrame frame;
        frame.line = line;
        const Bytes bytes = frame.encode();
        EXPECT_FALSE(isBinaryCommandFrame(bytes.data(), bytes.size())) << line;
    }
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    const ForwardPlan plan = gateway.forward("7680 12 /version");
    EXPECT_TRUE(isBinaryCommandFrame(plan.frame.data(), plan.frame.size()));
}

TEST(MixedVersions, AnIdANodeDoesNotKnowIsAnsweredWithUnknown) {
    Node node;
    // A newer gateway knows a command that this node has no id for.
    EXPECT_EQ(node.serve({0x20, 0x2F}), (Bytes{0xA0, 0x01}));
    EXPECT_EQ(node.serve({0x20, 0xFE, 0x01, 0x02}), (Bytes{0xA0, 0x01}));
    EXPECT_EQ(node.runs, 0);
    // Bytes behind the fields it knows are ignored: a newer gateway may append arguments.
    node.out.setU(0, 30);
    EXPECT_EQ(node.serve({0x20, 0x1A, 0x1F, 0x07, 0x07}), (Bytes{0xA0, 0x00, 0x1E}));
    EXPECT_EQ(node.seen.u(0), 30U);
}

TEST(MixedVersions, AReplyTheGatewayCannotReadIsShownAsBytes) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    const ForwardPlan plan = gateway.forward("7680 12 /role");
    // Fields that do not fit the declaration of the gateway.
    EXPECT_EQ(gateway.publish(plan, {0xA0, 0x00, 0x09, 0x09}), "12 Reply not understood: 0909");
    // An error keeps its status.
    EXPECT_EQ(gateway.publish(plan, {0xA0, 0x90, 0xFF}), "12 E144 ff");
    // Fields behind the ones the gateway knows are ignored: a newer node may append some.
    EXPECT_EQ(gateway.publish(plan, {0xA0, 0x00, 0x01, 0x01, 0x55}), "12 gateway (stored)");
}

// --- A node and its checks ---

TEST(NodeChecks, ArgumentsThatDoNotFitAreRefused) {
    Node node;
    // /role.set takes one name of two.
    EXPECT_EQ(node.serve({0x20, 0x11}), (Bytes{0xA0, 0x05}));
    EXPECT_EQ(node.serve({0x20, 0x11, 0x02}), (Bytes{0xA0, 0x05}));
    // A varint that is cut.
    EXPECT_EQ(node.serve({0x20, 0x7A, 0x80}), (Bytes{0xA0, 0x05}));
    EXPECT_EQ(node.runs, 0);
}

TEST(NodeChecks, ASignedCommandNeedsASignature) {
    Node node;
    // /report.set and /reboot without a signature.
    EXPECT_EQ(node.serve({0x20, 0x7A, 0xD8, 0x04}), (Bytes{0xA0, 0x02}));
    EXPECT_EQ(node.serve({0x20, 0x36}), (Bytes{0xA0, 0x02}));
    EXPECT_EQ(node.runs, 0);
    // An open command runs without one, and with a valid one.
    node.out.setU(0, 0).setU(1, 0);
    EXPECT_EQ(node.serve({0x20, 0x10}), (Bytes{0xA0, 0x00, 0x00, 0x00}));
    EXPECT_EQ(node.runs, 1);
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    const ForwardPlan plan = gateway.forward(signedMqtt(1, 5, "/role"));
    ASSERT_TRUE(plan.send());
    EXPECT_EQ(plan.frame.size(), 14U);
    EXPECT_EQ(node.serve(plan.frame), (Bytes{0xA0, 0x00, 0x00, 0x00}));
    EXPECT_EQ(node.runs, 2);
    EXPECT_EQ(node.stored, (std::vector<uint32_t>{5}));
}

TEST(NodeChecks, TheTagCoversTheCanonicalLine) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    const ForwardPlan plan = gateway.forward(signedMqtt(15, 0x65000001, "/report.set 600"));
    ASSERT_TRUE(plan.send());
    Node node;
    node.out.setU(0, 600);

    // Another argument, another counter, another tag, another node: each breaks the tag.
    Bytes changed = plan.frame;
    changed[2] = 0xD9;
    EXPECT_EQ(node.serve(changed), (Bytes{0xA0, 0x03}));
    changed = plan.frame;
    changed[4] ^= 0x01;
    EXPECT_EQ(node.serve(changed), (Bytes{0xA0, 0x03}));
    changed = plan.frame;
    changed.back() ^= 0x01;
    EXPECT_EQ(node.serve(changed), (Bytes{0xA0, 0x03}));
    // The same fields under the id of another command with one number.
    changed = plan.frame;
    changed[1] = 0x14;
    EXPECT_EQ(node.serve(changed), (Bytes{0xA0, 0x03}));
    Node other;
    other.address = 0x1A2B;
    EXPECT_EQ(other.serve(plan.frame), (Bytes{0xA0, 0x03}));
    Node keyless;
    keyless.hasKey = false;
    EXPECT_EQ(keyless.serve(plan.frame), (Bytes{0xA0, 0x03}));
    EXPECT_EQ(node.runs + other.runs + keyless.runs, 0);
    EXPECT_TRUE(node.stored.empty());

    EXPECT_EQ(node.serve(plan.frame), (Bytes{0xA0, 0x00, 0xD9, 0x04}));
    EXPECT_EQ(node.runs, 1);
}

TEST(NodeChecks, ACounterThatCannotBeStoredOrReadStopsTheCommand) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    const ForwardPlan plan = gateway.forward(signedMqtt(15, 0x65000001, "/report.set 600"));
    Node node;
    node.storageWorks = false;
    EXPECT_EQ(node.serve(plan.frame), (Bytes{0xA0, 0x06}));
    node.storageWorks = true;
    node.counterLoads = false;
    EXPECT_EQ(node.serve(plan.frame), (Bytes{0xA0, 0x06}));
    EXPECT_EQ(node.runs, 0);
}

// --- Signed replay through the binary path ---

TEST(BinaryReplay, ARetransmittedCommandRunsOnceAndGetsTheSameBytes) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    const ForwardPlan plan = gateway.forward(signedMqtt(15, 0x65000001, "/report.set 600"));
    Node node;
    node.out.setU(0, 600);
    const Bytes first = node.serve(plan.frame);
    EXPECT_EQ(first, (Bytes{0xA0, 0x00, 0xD9, 0x04}));
    // The reply was lost and the gateway sends the frame again. The node has changed since.
    node.out.setU(0, 999);
    const Bytes second = node.serve(plan.frame);
    EXPECT_EQ(second, first);
    EXPECT_EQ(node.runs, 1);
    EXPECT_EQ(gateway.publish(plan, second), "15 Report interval set to 600 s");
    EXPECT_EQ(node.stored.size(), 1U);
}

TEST(BinaryReplay, AnOlderCounterIsRefused) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    Node node;
    node.out.setU(0, 600);
    const ForwardPlan newer = gateway.forward(signedMqtt(15, 0x65000002, "/report.set 600"));
    const ForwardPlan older = gateway.forward(signedMqtt(16, 0x65000001, "/report.set 600"));
    EXPECT_EQ(node.serve(newer.frame), (Bytes{0xA0, 0x00, 0xD9, 0x04}));
    const Bytes replay = node.serve(older.frame);
    EXPECT_EQ(replay, (Bytes{0xA0, 0x04}));
    EXPECT_EQ(gateway.publish(older, replay), "16 E4");
    EXPECT_EQ(node.runs, 1);
}

TEST(BinaryReplay, AnErrorIsRepeatedWithItsNumber) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    const ForwardPlan plan = gateway.forward(signedMqtt(15, 0x65000001, "/report.set 600"));
    Node node;
    node.result = TypedResult(128, 0x1105);
    EXPECT_EQ(node.serve(plan.frame), (Bytes{0xA0, 0x80, 0x85, 0x22}));
    node.result = TypedResult();
    EXPECT_EQ(node.serve(plan.frame), (Bytes{0xA0, 0x80, 0x85, 0x22}));
    EXPECT_EQ(node.runs, 1);
}

TEST(BinaryReplay, AfterARestartBitFourSaysTheReplyWasLost) {
    Gateway gateway;
    gateway.binaryNodes.note(NODE, true);
    const ForwardPlan plan = gateway.forward(signedMqtt(15, 0x65000001, "/role.set gateway"));
    Node node;
    EXPECT_EQ(node.serve(plan.frame), (Bytes{0xA0, 0x00}));
    // /role.set restarts the node. The retransmission meets a node that kept only the counter.
    node.restart();
    const Bytes again = node.serve(plan.frame);
    EXPECT_EQ(again, (Bytes{0xB0, 0x00}));
    EXPECT_EQ(node.runs, 1);
    EXPECT_EQ(gateway.publish(plan, again), "15 OK already executed");
    // An older counter stays refused after the restart.
    const ForwardPlan older = gateway.forward(signedMqtt(16, 0x65000000, "/role.set sensor"));
    EXPECT_EQ(node.serve(older.frame), (Bytes{0xA0, 0x04}));
    // The bit never comes with reply fields, so a command with a reply reads the same way.
    const ForwardPlan report = gateway.forward(signedMqtt(17, 0x65000005, "/report.set 600"));
    node.out.setU(0, 600);
    EXPECT_EQ(node.serve(report.frame), (Bytes{0xA0, 0x00, 0xD9, 0x04}));
    node.restart();
    EXPECT_EQ(node.serve(report.frame), (Bytes{0xB0, 0x00}));
    EXPECT_EQ(gateway.publish(report, {0xB0, 0x00}), "17 OK already executed");
}

TEST(BinaryReplay, TheCounterIsSharedWithTheTextPath) {
    // The reply cache of a counter holds the form it was given in.
    CommandCounter counter;
    auto store = [](uint32_t) { return true; };
    int runs = 0;
    // The command ran from a text frame. Its binary repeat gets the status alone.
    CommandResult text = runOncePerCounter(counter, 7, store, [&runs]() {
        runs++;
        return CommandResult("Report interval set to 600 s");
    });
    EXPECT_EQ(text.text, "Report interval set to 600 s");
    BinaryReply binary = runBinaryOncePerCounter(counter, 7, store, [&runs]() {
        runs++;
        return BinaryReply();
    });
    EXPECT_EQ(binary.status, 0);
    EXPECT_TRUE(binary.replayed);
    EXPECT_TRUE(binary.payload.empty());
    EXPECT_EQ(runs, 1);
    // The text repeat still gets the text.
    EXPECT_EQ(runOncePerCounter(counter, 7, store, []() { return CommandResult("x"); }).text,
              "Report interval set to 600 s");

    // The command ran from a binary frame. Its text repeat gets the fixed text.
    BinaryReply ran;
    ran.payload = {0xD9, 0x04};
    binary = runBinaryOncePerCounter(counter, 8, store, [&ran]() { return ran; });
    EXPECT_EQ(binary.payload, (Bytes{0xD9, 0x04}));
    EXPECT_FALSE(binary.replayed);
    text = runOncePerCounter(counter, 8, store, []() { return CommandResult("x"); });
    EXPECT_TRUE(text.ok());
    EXPECT_EQ(text.text, "OK already executed");
    EXPECT_EQ(runBinaryOncePerCounter(counter, 8, store, []() { return BinaryReply(); }).payload,
              (Bytes{0xD9, 0x04}));

    // An error keeps its status in the other form, without its argument.
    ran.status = 129;
    ran.payload = {0x85, 0x22};
    runBinaryOncePerCounter(counter, 9, store, [&ran]() { return ran; });
    text = runOncePerCounter(counter, 9, store, []() { return CommandResult("x"); });
    EXPECT_EQ(text.status, 129);
    EXPECT_EQ(text.text, "");
    runOncePerCounter(counter, 10, store, []() { return CommandResult(130, "ESP_FAIL"); });
    binary = runBinaryOncePerCounter(counter, 10, store, []() { return BinaryReply(); });
    EXPECT_EQ(binary.status, 130);
    EXPECT_FALSE(binary.replayed);
    EXPECT_TRUE(binary.payload.empty());
    // Older counters are refused in both forms.
    EXPECT_EQ(runBinaryOncePerCounter(counter, 9, store, []() { return BinaryReply(); }).status,
              static_cast<uint8_t>(CommandStatus::REPLAY));
    EXPECT_EQ(runBinaryOncePerCounter(
                  counter, 11, [](uint32_t) { return false; }, []() { return BinaryReply(); })
                  .status,
              static_cast<uint8_t>(CommandStatus::STORAGE));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
