#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "cmdAuth.h"
#include "cmdFrame.h"
#include "cmdPerm.h"
#include "cmdSeal.h"
#include "cmdText.h"
#include "cmdVectors.h"
#include "deploymentKey.h"

namespace {

/** Records what it was asked to sign and answers with a fixed digest. */
struct FakeHmac {
    std::vector<uint8_t> lastMessage;
    HmacDigest digest{};

    FakeHmac() {
        for (size_t i = 0; i < digest.size(); i++) digest[i] = static_cast<uint8_t>(0xA0 + i);
    }

    KeyedHmac function() {
        return [this](const uint8_t* data, size_t size) {
            lastMessage.assign(data, data + size);
            return digest;
        };
    }
};

KeyedHmac keyed(const uint8_t* key, size_t keySize) {
    std::vector<uint8_t> copy(key, key + keySize);
    return [copy](const uint8_t* data, size_t size) {
        return hmacSha256(copy.data(), copy.size(), data, size);
    };
}

/** Kw = HMAC(K, "LMW1"), the key of the argument keystream. */
KeyedHmac sealHmac() {
    const uint8_t label[] = {'L', 'M', 'W', '1'};
    Sha256 sealKey = hmacSha256(CMD_VECTOR_KEY, sizeof(CMD_VECTOR_KEY), label, sizeof(label));
    return keyed(sealKey.data(), sealKey.size());
}

std::vector<uint8_t> hexBytes(const std::string& text) {
    std::vector<uint8_t> out;
    EXPECT_TRUE(fromHex(text, out)) << text;
    return out;
}

}  // namespace

TEST(CmdPerm, OpenCommandsRunEverywhere) {
    for (Origin origin : {Origin::SERIAL_CONSOLE, Origin::MQTT, Origin::LORA}) {
        EXPECT_TRUE(commandPermitted(Perm::OPEN, origin, false));
    }
}

TEST(CmdPerm, SignedCommandsNeedASignatureRemotely) {
    EXPECT_TRUE(commandPermitted(Perm::SIGNED, Origin::SERIAL_CONSOLE, false));
    EXPECT_FALSE(commandPermitted(Perm::SIGNED, Origin::LORA, false));
    EXPECT_FALSE(commandPermitted(Perm::SIGNED, Origin::MQTT, false));
    EXPECT_TRUE(commandPermitted(Perm::SIGNED, Origin::LORA, true));
    EXPECT_TRUE(commandPermitted(Perm::SIGNED, Origin::MQTT, true));
}

TEST(CmdPerm, LocalOnlyCommandsNeverRunRemotely) {
    EXPECT_TRUE(commandPermitted(Perm::LOCAL_ONLY, Origin::SERIAL_CONSOLE, false));
    EXPECT_FALSE(commandPermitted(Perm::LOCAL_ONLY, Origin::LORA, true));
    EXPECT_FALSE(commandPermitted(Perm::LOCAL_ONLY, Origin::MQTT, true));
}

TEST(CmdAuth, MessageBindsDestinationCounterAndLine) {
    std::vector<uint8_t> message = commandAuthMessage(0x1A2B, 0x01020304, "/reboot");
    std::vector<uint8_t> expected = {'L', 'M', 'C', '1', 0x2B, 0x1A, 0x04, 0x03, 0x02, 0x01,
                                     '/', 'r', 'e', 'b', 'o', 'o', 't'};
    EXPECT_EQ(message, expected);
}

TEST(CmdAuth, TagIsTheTruncatedHmacOfTheMessage) {
    FakeHmac hmac;
    CommandTag tag = commandTag(hmac.function(), 0x1A2B, 7, "/reboot");
    EXPECT_EQ(hmac.lastMessage, commandAuthMessage(0x1A2B, 7, "/reboot"));
    for (size_t i = 0; i < tag.size(); i++) EXPECT_EQ(tag[i], hmac.digest[i]);
}

TEST(CmdAuth, TagsCompare) {
    CommandTag a{{1, 2, 3, 4, 5, 6, 7, 8}};
    CommandTag b = a;
    EXPECT_TRUE(commandTagsEqual(a, b));
    for (size_t i = 0; i < b.size(); i++) {
        CommandTag c = a;
        c[i] ^= 0x01;
        EXPECT_FALSE(commandTagsEqual(a, c)) << i;
    }
}

TEST(CmdCounter, OnlyNewerCountersRun) {
    CommandCounter counter;
    EXPECT_EQ(counter.decide(0), CounterDecision::EXECUTE);
    counter.accept(100);
    EXPECT_EQ(counter.decide(101), CounterDecision::EXECUTE);
    EXPECT_EQ(counter.decide(100), CounterDecision::REPEAT);
    EXPECT_EQ(counter.decide(99), CounterDecision::REPLAY);
    EXPECT_EQ(counter.last(), 100u);
}

TEST(CmdCounter, StoredCounterSurvivesARestart) {
    CommandCounter counter(500, true);
    EXPECT_EQ(counter.decide(499), CounterDecision::REPLAY);
    EXPECT_EQ(counter.decide(500), CounterDecision::REPEAT);
    EXPECT_EQ(counter.decide(501), CounterDecision::EXECUTE);
}

TEST(CmdCounter, RepeatReturnsTheStoredReply) {
    CommandCounter counter;
    counter.accept(7);
    EXPECT_EQ(counter.lastReply().status, 0);
    EXPECT_EQ(counter.lastReply().text, "OK already executed");
    counter.setReply("OK maint 3600");
    EXPECT_EQ(counter.lastReply().text, "OK maint 3600");
    counter.accept(8);
    EXPECT_NE(counter.lastReply().text, "OK maint 3600");
}

TEST(CmdCounter, ARetransmittedCommandGetsTheCachedReplyAndRunsOnce) {
    CommandCounter counter(41, true);
    int runs = 0;
    std::vector<uint32_t> stored;
    auto store = [&stored](uint32_t value) {
        stored.push_back(value);
        return true;
    };
    auto execute = [&runs]() {
        runs++;
        return CommandResult("OK install 600 s");
    };
    EXPECT_EQ(runOncePerCounter(counter, 42, store, execute).text, "OK install 600 s");
    // The gateway sends the same frame again because the reply was lost.
    EXPECT_EQ(runOncePerCounter(counter, 42, store, execute).text, "OK install 600 s");
    EXPECT_EQ(runOncePerCounter(counter, 42, store, execute).status, 0);
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(stored, std::vector<uint32_t>{42});
    // A retransmission that arrives after a newer command is refused.
    EXPECT_EQ(runOncePerCounter(counter, 43, store, execute).text, "OK install 600 s");
    CommandResult replay = runOncePerCounter(counter, 42, store, execute);
    EXPECT_EQ(replay.status, static_cast<uint8_t>(CommandStatus::REPLAY));
    EXPECT_EQ(replay.text, "");
    EXPECT_EQ(runs, 2);
}

TEST(CmdCounter, ARepeatedCounterGetsTheCachedStatusWithItsArgument) {
    CommandCounter counter;
    int runs = 0;
    auto store = [](uint32_t) { return true; };
    auto execute = [&runs]() {
        runs++;
        return CommandResult(131, "REFUSE_BATTERY");
    };
    CommandResult first = runOncePerCounter(counter, 9, store, execute);
    CommandResult again = runOncePerCounter(counter, 9, store, execute);
    EXPECT_EQ(first.status, 131);
    EXPECT_EQ(again.status, 131);
    EXPECT_EQ(again.text, "REFUSE_BATTERY");
    EXPECT_EQ(runs, 1);
}

TEST(CmdCounter, ARetransmissionAfterARebootDoesNotRunAgain) {
    // The node stored counter 42, ran the command and restarted before its reply arrived.
    CommandCounter counter(42, true);
    int runs = 0;
    auto store = [](uint32_t) { return true; };
    auto execute = [&runs]() {
        runs++;
        return CommandResult("OK");
    };
    CommandResult reply = runOncePerCounter(counter, 42, store, execute);
    EXPECT_EQ(reply.status, 0);
    EXPECT_EQ(reply.text, "OK already executed");
    EXPECT_EQ(runs, 0);
}

TEST(CmdCounter, ACommandWhoseCounterCannotBeStoredDoesNotRun) {
    CommandCounter counter;
    int runs = 0;
    auto execute = [&runs]() {
        runs++;
        return CommandResult("OK");
    };
    EXPECT_EQ(runOncePerCounter(
                  counter, 7, [](uint32_t) { return false; }, execute)
                  .status,
              static_cast<uint8_t>(CommandStatus::STORAGE));
    EXPECT_EQ(runs, 0);
    EXPECT_EQ(runOncePerCounter(counter, 7, [](uint32_t) { return true; }, execute).text, "OK");
    EXPECT_EQ(runs, 1);
}

TEST(CmdFrame, UnsignedFrameRoundTrips) {
    CommandFrame frame;
    frame.line = "/version";
    std::vector<uint8_t> bytes = frame.encode();
    ASSERT_EQ(bytes.size(), 1 + frame.line.size());
    EXPECT_EQ(bytes[0], 0);

    CommandFrame out;
    ASSERT_TRUE(CommandFrame::decode(bytes.data(), bytes.size(), out));
    EXPECT_FALSE(out.isSigned);
    EXPECT_FALSE(out.response);
    EXPECT_EQ(out.line, "/version");
}

TEST(CmdFrame, SignedFrameCarriesCounterAndTag) {
    CommandFrame frame;
    frame.isSigned = true;
    frame.line = "/maint.open 3600";
    frame.counter = 0x01020304;
    frame.tag = {{9, 8, 7, 6, 5, 4, 3, 2}};
    std::vector<uint8_t> bytes = frame.encode();
    ASSERT_EQ(bytes.size(), 1 + frame.line.size() + CommandFrame::SIGNATURE_SIZE);
    EXPECT_EQ(bytes[0], CommandFrame::FLAG_SIGNED);
    size_t at = 1 + frame.line.size();
    EXPECT_EQ(bytes[at], 0x04);
    EXPECT_EQ(bytes[at + 3], 0x01);
    EXPECT_EQ(bytes[at + 4], 9);

    CommandFrame out;
    ASSERT_TRUE(CommandFrame::decode(bytes.data(), bytes.size(), out));
    EXPECT_TRUE(out.isSigned);
    EXPECT_EQ(out.line, "/maint.open 3600");
    EXPECT_EQ(out.counter, 0x01020304u);
    EXPECT_EQ(out.tag, frame.tag);
}

TEST(CmdFrame, ASuccessReplyCarriesStatusZeroBeforeItsText) {
    CommandFrame frame;
    frame.response = true;
    frame.line = "OK install 600 s";
    std::vector<uint8_t> bytes = frame.encode();
    EXPECT_EQ(toHex(bytes.data(), bytes.size()), "80004f4b20696e7374616c6c203630302073");
    CommandFrame out;
    ASSERT_TRUE(CommandFrame::decode(bytes.data(), bytes.size(), out));
    EXPECT_TRUE(out.response);
    EXPECT_FALSE(out.isSigned);
    EXPECT_EQ(out.status, 0);
    EXPECT_EQ(out.line, "OK install 600 s");
}

TEST(CmdFrame, AnErrorReplyCarriesItsStatusAndAnOptionalArgument) {
    CommandFrame frame;
    frame.response = true;
    frame.status = static_cast<uint8_t>(CommandStatus::ARG);
    std::vector<uint8_t> bytes = frame.encode();
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0x80, 0x05}));
    CommandFrame out;
    ASSERT_TRUE(CommandFrame::decode(bytes.data(), bytes.size(), out));
    EXPECT_EQ(out.status, 5);
    EXPECT_EQ(out.line, "");

    frame.status = 131;
    frame.line = "REFUSE_BATTERY";
    bytes = frame.encode();
    EXPECT_EQ(toHex(bytes.data(), bytes.size()), "80835245465553455f42415454455259");
    ASSERT_TRUE(CommandFrame::decode(bytes.data(), bytes.size(), out));
    EXPECT_EQ(out.status, 131);
    EXPECT_EQ(out.line, "REFUSE_BATTERY");
}

TEST(CmdFrame, ARequestCarriesNoStatusByte) {
    CommandFrame frame;
    frame.status = 9;
    frame.line = "/x";
    EXPECT_EQ(frame.encode(), (std::vector<uint8_t>{0x00, '/', 'x'}));
    CommandFrame out;
    out.status = 7;
    std::vector<uint8_t> bytes = frame.encode();
    ASSERT_TRUE(CommandFrame::decode(bytes.data(), bytes.size(), out));
    EXPECT_EQ(out.status, 0);
}

TEST(CmdFrame, RejectsMalformedFrames) {
    CommandFrame out;
    EXPECT_FALSE(CommandFrame::decode(nullptr, 0, out));
    const uint8_t shortSigned[] = {CommandFrame::FLAG_SIGNED, '/', 'x', 1, 2, 3};
    EXPECT_FALSE(CommandFrame::decode(shortSigned, sizeof(shortSigned), out));
    const uint8_t nul[] = {0, '/', 0, 'x'};
    EXPECT_FALSE(CommandFrame::decode(nul, sizeof(nul), out));
    const uint8_t empty[] = {0};
    EXPECT_FALSE(CommandFrame::decode(empty, sizeof(empty), out));
    const uint8_t replyWithoutStatus[] = {CommandFrame::FLAG_RESPONSE};
    EXPECT_FALSE(CommandFrame::decode(replyWithoutStatus, sizeof(replyWithoutStatus), out));
    const uint8_t signedReply[] = {CommandFrame::FLAG_RESPONSE | CommandFrame::FLAG_SIGNED,
                                   0,
                                   'O',
                                   'K',
                                   1,
                                   2,
                                   3,
                                   4,
                                   5,
                                   6,
                                   7,
                                   8,
                                   9,
                                   10,
                                   11,
                                   12};
    EXPECT_FALSE(CommandFrame::decode(signedReply, sizeof(signedReply), out));
    const uint8_t replyWithNul[] = {CommandFrame::FLAG_RESPONSE, 0, 'O', 0};
    EXPECT_FALSE(CommandFrame::decode(replyWithNul, sizeof(replyWithNul), out));
}

// Vectors are computed by scripts/ota_tools/lmcmd.py with Python's hmac module.
TEST(CmdVectors, TagsMatchThePythonTool) {
    KeyedHmac hmac = keyed(CMD_VECTOR_KEY, sizeof(CMD_VECTOR_KEY));
    for (const CmdTagVector& vector : CMD_TAG_VECTORS) {
        CommandTag tag = commandTag(hmac, vector.dst, vector.counter, vector.line);
        EXPECT_EQ(toHex(tag.data(), tag.size()), vector.tag) << vector.line;
        EXPECT_EQ("@" + std::string(vector.serial).substr(1, 4) + " " + vector.line +
                      formatSignature(vector.counter, tag),
                  vector.serial);
    }
}

TEST(CmdText, ParsesASignedRemoteLine) {
    const CmdTagVector& vector = CMD_TAG_VECTORS[1];
    CommandLine parsed;
    ASSERT_TRUE(parseCommandLine(vector.serial, parsed));
    EXPECT_TRUE(parsed.remote);
    EXPECT_EQ(parsed.dst, vector.dst);
    EXPECT_EQ(parsed.line, vector.line);
    EXPECT_TRUE(parsed.isSigned);
    EXPECT_EQ(parsed.counter, vector.counter);
    EXPECT_EQ(toHex(parsed.tag.data(), parsed.tag.size()), vector.tag);
}

TEST(CmdText, ParsesPlainLocalLines) {
    CommandLine parsed;
    ASSERT_TRUE(parseCommandLine("/role.set sensor", parsed));
    EXPECT_FALSE(parsed.remote);
    EXPECT_FALSE(parsed.isSigned);
    EXPECT_EQ(parsed.line, "/role.set sensor");

    ASSERT_TRUE(parseCommandLine("@7680 /version", parsed));
    EXPECT_TRUE(parsed.remote);
    EXPECT_EQ(parsed.dst, 0x7680);
    EXPECT_EQ(parsed.line, "/version");
    EXPECT_FALSE(parsed.isSigned);

    ASSERT_TRUE(parseCommandLine("/displayText # not a signature", parsed));
    EXPECT_EQ(parsed.line, "/displayText # not a signature");
    EXPECT_FALSE(parsed.isSigned);
}

TEST(CmdText, RejectsMalformedLines) {
    CommandLine parsed;
    EXPECT_FALSE(parseCommandLine("", parsed));
    EXPECT_FALSE(parseCommandLine("@76 /version", parsed));
    EXPECT_FALSE(parseCommandLine("@76zz /version", parsed));
    EXPECT_FALSE(parseCommandLine("@7680", parsed));
    EXPECT_FALSE(parseCommandLine("@7680 ", parsed));
    EXPECT_FALSE(parseCommandLine("/reboot #0000000g.d15620d50289ed6f", parsed));
    EXPECT_FALSE(parseCommandLine("/reboot #00000001.d15620d50289ed6", parsed));
    EXPECT_FALSE(parseCommandLine("#00000001.d15620d50289ed6f", parsed));
}

TEST(CmdText, SplitsNameAndArguments) {
    std::string name, args;
    splitCommand("/maint.open  600 ap ", name, args);
    EXPECT_EQ(name, "/maint.open");
    EXPECT_EQ(args, "600 ap");
    splitCommand("/version", name, args);
    EXPECT_EQ(name, "/version");
    EXPECT_EQ(args, "");
}

TEST(CmdText, ReadsTheAddressOfAddressedArguments) {
    uint16_t dst = 0;
    std::string rest;
    ASSERT_TRUE(parseAddressedArgs("0A3F hello there", dst, rest));
    EXPECT_EQ(dst, 0x0A3F);
    EXPECT_EQ(rest, "hello there");
    ASSERT_TRUE(parseAddressedArgs("beef", dst, rest));
    EXPECT_EQ(dst, 0xBEEF);
    EXPECT_EQ(rest, "");
    ASSERT_TRUE(parseAddressedArgs("0add  milk", dst, rest));
    EXPECT_EQ(dst, 0x0ADD);
    EXPECT_EQ(rest, " milk");
}

TEST(CmdText, OnlyFourHexDigitsAreAnAddress) {
    uint16_t dst = 7;
    std::string rest = "kept";
    for (const char* args : {"", "add milk", "a", "6c", "0A3F5 text", "cafe1", "0x1A text",
                             "face-to-face", "milk 0A3F", " 0A3F text"}) {
        EXPECT_FALSE(parseAddressedArgs(args, dst, rest)) << args;
    }
    EXPECT_EQ(dst, 7);
    EXPECT_EQ(rest, "kept");
}

TEST(CmdText, ParsesMqttCommands) {
    MqttCommand command;
    ASSERT_TRUE(parseMqttCommand("7680 12 /maint.open 600 #65000000.48fbaa33fa0d63c5", command));
    EXPECT_EQ(command.dst, 0x7680);
    EXPECT_EQ(command.requestId, 12);
    EXPECT_EQ(command.command.line, "/maint.open 600");
    EXPECT_TRUE(command.command.isSigned);
    EXPECT_FALSE(command.command.remote);

    EXPECT_FALSE(parseMqttCommand("7680 300 /version", command));
    EXPECT_FALSE(parseMqttCommand("7680 1 @1A2B /version", command));
    EXPECT_FALSE(parseMqttCommand("7680 /version", command));
    EXPECT_FALSE(parseMqttCommand("xyz 1 /version", command));
}

TEST(CmdText, HexRoundTrips) {
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(fromHex("00ff10Ab", bytes));
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0x00, 0xFF, 0x10, 0xAB}));
    EXPECT_EQ(toHex(bytes.data(), bytes.size()), "00ff10ab");
    EXPECT_FALSE(fromHex("abc", bytes));
    EXPECT_FALSE(fromHex("zz", bytes));
}

TEST(CmdSeal, MatchesThePythonTool) {
    std::vector<uint8_t> sealed =
        sealCommandArgs(sealHmac(), CMD_SEAL_DST, CMD_SEAL_COUNTER, hexBytes(CMD_SEAL_PLAIN));
    EXPECT_EQ(toHex(sealed.data(), sealed.size()), CMD_SEAL_SEALED);
    EXPECT_EQ(sealCommandArgs(sealHmac(), CMD_SEAL_DST, CMD_SEAL_COUNTER, sealed),
              hexBytes(CMD_SEAL_PLAIN));
}

TEST(CmdSeal, OpensTheWifiLineOfThePythonTool) {
    std::string name, args;
    splitCommand(CMD_WIFI_LINE, name, args);
    EXPECT_EQ(name, "/maint.wifi");
    std::vector<uint8_t> plain = sealCommandArgs(sealHmac(), 0x7680, 9, hexBytes(args));
    std::string ssid, password;
    ASSERT_TRUE(decodeWifiCredentials(plain, ssid, password));
    EXPECT_EQ(ssid, CMD_WIFI_SSID);
    EXPECT_EQ(password, CMD_WIFI_PASSWORD);
    EXPECT_EQ(encodeWifiCredentials(ssid, password), plain);
}

TEST(CmdSeal, ValidatesWifiCredentials) {
    std::string ssid, password;
    auto decode = [&](const std::string& s, const std::string& p) {
        return decodeWifiCredentials(encodeWifiCredentials(s, p), ssid, password);
    };
    EXPECT_TRUE(decode("OpenNet", ""));
    EXPECT_TRUE(decode(std::string(32, 's'), std::string(63, 'p')));
    EXPECT_FALSE(decode("", "password123"));
    EXPECT_FALSE(decode(std::string(33, 's'), "password123"));
    EXPECT_FALSE(decode("ssid", "short"));
    EXPECT_FALSE(decode("ssid", std::string(64, 'p')));
    EXPECT_FALSE(decode("ssid", std::string("pass\x01word")));
    std::vector<uint8_t> noSeparator = {'s', 's', 'i', 'd'};
    EXPECT_FALSE(decodeWifiCredentials(noSeparator, ssid, password));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
