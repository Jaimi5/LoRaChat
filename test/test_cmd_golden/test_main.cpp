#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <vector>

#include "cmdCatalogue.h"
#include "cmdFields.h"
#include "cmdFrame.h"
#include "cmdTyped.h"
#include "firmwareCatalogue.h"
#include "typedFrames.h"
// The fixture reads limits from NetCore and OtaCore. These includes let PlatformIO find both.
#include "installMode.h"
#include "maintWindowPlan.h"

/**
 * Golden bytes of every command that can be sent to another node: its request frame, its reply
 * frames and the text a person reads. The reply fields are set here as the handlers in src/ set
 * them, by position.
 */
namespace {

using Bytes = std::vector<uint8_t>;

/** Most bytes of a command frame at SF9: one LoRa packet without the mesh and app headers. */
constexpr size_t FRAME_MAX_BYTES = 102;
/** Most bytes of the reply fields in such a frame. */
constexpr size_t REPLY_FIELDS_MAX_BYTES = FRAME_MAX_BYTES - BinaryCommandFrame::HEADER_SIZE;

/** @return @p frame followed by the signature of the golden vectors. */
Bytes withSignature(Bytes frame) {
    const Bytes signature = goldenSignature();
    frame.insert(frame.end(), signature.begin(), signature.end());
    return frame;
}

/** Checks the request frame of @p line and the line a node rebuilds from it. */
void expectRequest(const std::string& name, const std::string& line, bool isSigned,
                   const Bytes& expected) {
    const TypedDeclaration declaration = firmwareDeclaration(name);
    const Bytes request = requestFrame(declaration, name, line, isSigned);
    EXPECT_EQ(request, isSigned ? withSignature(expected) : expected) << line;
    EXPECT_EQ(lineOf(declaration, name, request), line);
    EXPECT_LE(request.size(), FRAME_MAX_BYTES) << line;
}

/** @return the arguments of @p line as fields. */
Fields argsOf(const std::string& name, const std::string& line) {
    Fields in;
    EXPECT_TRUE(parseArgs(firmwareDeclaration(name).args, line.substr(name.size()), in)) << line;
    return in;
}

/**
 * Checks a successful reply: its frame, the text a gateway makes of the frame, and the same
 * text when the command runs from a text line on the node itself.
 */
void expectReply(const std::string& name, const std::string& line, const Fields& out,
                 const Bytes& expected, const std::string& text) {
    const TypedDeclaration declaration = firmwareDeclaration(name);
    const Bytes reply = replyFrame(declaration, TypedResult(), out);
    EXPECT_EQ(reply, expected) << line;
    EXPECT_LE(reply.size(), FRAME_MAX_BYTES) << line;
    const CommandResult remote = textOf(declaration, argsOf(name, line), reply);
    EXPECT_TRUE(remote.ok()) << line;
    EXPECT_EQ(remote.text, text);
    const CommandResult local = runTypedFromText(
        declaration,
        [&out](const Fields&, Fields& written) {
            written = out;
            return TypedResult();
        },
        line.substr(name.size()));
    EXPECT_TRUE(local.ok()) << line;
    EXPECT_EQ(local.text, text);
}

/** Checks an error reply: its frame and the status and argument a gateway reads from it. */
void expectError(const std::string& name, const TypedResult& result, const Bytes& expected,
                 const std::string& argument) {
    const TypedDeclaration declaration = firmwareDeclaration(name);
    const Bytes reply = replyFrame(declaration, result, Fields());
    EXPECT_EQ(reply, expected) << name;
    const CommandResult remote = textOf(declaration, Fields(), reply);
    EXPECT_EQ(remote.status, result.status) << name;
    EXPECT_EQ(remote.text, argument) << name;
}

/** The reply fields of /sensor as Bme280Sensor::measureInto() writes them. */
Fields sensorReading(bool bme, uint8_t address, int32_t tempCentiC, uint32_t pressurePa,
                     uint32_t rhCentiPercent) {
    Fields out;
    out.setU(0, bme ? 1 : 0).setBytes(1, &address, 1).setI(2, tempCentiC).setU(3, pressurePa);
    if (bme) out.setU(4, rhCentiPercent);
    return out;
}

/** The reply fields of /version as OtaService::versionInto() writes them. */
Fields versionReply(bool dirty) {
    const uint8_t bootloader[] = {0x20, 0xF4, 0x87};
    const uint8_t commit[] = {0x2B, 0xFD, 0xEE, 0xB1};
    Fields out;
    out.setVersion(0, 0, 1, 2).setU(1, 0).setU(2, 0).setBytes(3, bootloader, 3).setU(4, 0);
    out.setBytes(5, commit, 4);
    if (dirty) out.setU(6, 0);
    return out;
}

/** One row of /getRT as LoRaMeshService::routesInto() writes it. */
Fields routeRow(uint16_t destination, uint16_t nextHop, uint8_t linkQuality, uint8_t hops,
                int rssiDbm, int snrCentiDb) {
    const uint8_t dst[] = {static_cast<uint8_t>(destination >> 8),
                           static_cast<uint8_t>(destination & 0xFF)};
    const uint8_t via[] = {static_cast<uint8_t>(nextHop >> 8),
                           static_cast<uint8_t>(nextHop & 0xFF)};
    Fields row;
    row.setBytes(0, dst, 2).setBytes(1, via, 2).setU(2, linkQuality).setU(3, hops);
    if (rssiDbm != 0) row.setU(4, static_cast<uint32_t>(-rssiDbm)).setI(5, snrCentiDb);
    return row;
}

}  // namespace

// --- Node ---

TEST(GoldenNode, Role) {
    expectRequest("/role", "/role", false, {0x20, 0x10});
    Fields out;
    out.setU(0, 0).setU(1, 0);
    expectReply("/role", "/role", out, {0xA0, 0x00, 0x00, 0x00}, "sensor (default)");
    out.setU(0, 1).setU(1, 1);
    expectReply("/role", "/role", out, {0xA0, 0x00, 0x01, 0x01}, "gateway (stored)");
}

TEST(GoldenNode, RoleSet) {
    expectRequest("/role.set", "/role.set gateway", true, {0x60, 0x11, 0x01});
    expectRequest("/role.set", "/role.set sensor", true, {0x60, 0x11, 0x00});
    expectReply("/role.set", "/role.set gateway", Fields(), {0xA0, 0x00},
                "Role set to gateway, restarting");
    expectReply("/role.set", "/role.set sensor", Fields(), {0xA0, 0x00},
                "Role set to sensor, restarting");
    // A person may type the name in any case. The signed line has the declared one.
    const TypedDeclaration declaration = firmwareDeclaration("/role.set");
    EXPECT_EQ(argsOf("/role.set", "/role.set Gateway").u(0), 1U);
    EXPECT_FALSE(isCanonicalLine("/role.set Gateway", "/role.set", declaration.args));
    Fields in;
    EXPECT_FALSE(parseArgs(declaration.args, "", in));
    EXPECT_FALSE(parseArgs(declaration.args, "relay", in));
    // The three errors of a stored setting. ESP_ERR_NVS_NOT_ENOUGH_SPACE is 0x1105.
    expectError("/role.set", TypedResult(128), {0xA0, 0x80}, "");
    expectError("/role.set", TypedResult(129, 0x1105), {0xA0, 0x81, 0x85, 0x22}, "0x1105");
    expectError("/role.set", TypedResult(130), {0xA0, 0x82}, "");
}

TEST(GoldenNode, Sleep) {
    expectRequest("/sleep", "/sleep", false, {0x20, 0x12});
    // Light sleep and 20 % stored, 12 light sleeps of 340 s in all, 3 ended by the power key.
    Fields stored;
    stored.setU(0, 1).setU(1, 1).setU(2, 20).setU(4, 12).setU(5, 340).setU(6, 0).setU(7, 0);
    stored.setU(8, 3);
    expectReply("/sleep", "/sleep", stored,
                {0xA0, 0x00, 0x01, 0x01, 0x15, 0x00, 0x0C, 0xD4, 0x02, 0x00, 0x00, 0x04},
                "sleep light (stored), fraction 20 % (stored), light 12 x 340 s, deep 0 x 0 s "
                "since power-on, 3 light sleeps ended by the power key");
    // Nothing stored and no sleep yet.
    Fields fresh;
    fresh.setU(0, 0).setU(1, 0).setU(3, 0).setU(4, 0).setU(5, 0).setU(6, 0).setU(7, 0).setU(8, 0);
    expectReply("/sleep", "/sleep", fresh,
                {0xA0, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01},
                "sleep none (default), fraction default, light 0 x 0 s, deep 0 x 0 s since "
                "power-on, 0 light sleeps ended by the power key");
    // A build where the power key does not wake the node leaves the last field out.
    Fields deep;
    deep.setU(0, 2).setU(1, 1).setU(2, 0).setU(4, 300).setU(5, 4000).setU(6, 25).setU(7, 90000);
    expectReply(
        "/sleep", "/sleep", deep,
        {0xA0, 0x00, 0x02, 0x01, 0x01, 0x00, 0xAC, 0x02, 0xA0, 0x1F, 0x19, 0x90, 0xBF, 0x05},
        "sleep deep (stored), fraction 0 % (stored), light 300 x 4000 s, deep 25 x 90000 "
        "s since power-on");
}

TEST(GoldenNode, SleepReplyIsCompleteWithTheLargestCounters) {
    const TypedDeclaration declaration = firmwareDeclaration("/sleep");
    Fields out;
    out.setU(0, 2).setU(1, 1).setU(2, 90);
    for (size_t field = 4; field <= 8; field++) out.setU(field, UINT32_MAX);
    const Bytes reply = replyFrame(declaration, TypedResult(), out);
    EXPECT_EQ(reply.size(), 31U);
    EXPECT_EQ(textOf(declaration, Fields(), reply).text,
              "sleep deep (stored), fraction 90 % (stored), light 4294967295 x 4294967295 s, deep "
              "4294967295 x 4294967295 s since power-on, 4294967295 light sleeps ended by the "
              "power key");
}

TEST(GoldenNode, SleepSet) {
    expectRequest("/sleep.set", "/sleep.set light", true, {0x60, 0x13, 0x01});
    expectRequest("/sleep.set", "/sleep.set none", true, {0x60, 0x13, 0x00});
    expectRequest("/sleep.set", "/sleep.set deep", true, {0x60, 0x13, 0x02});
    expectReply("/sleep.set", "/sleep.set light", Fields(), {0xA0, 0x00},
                "Sleep mode set to light, restarting");
    expectError("/sleep.set", TypedResult(129, 0x1105), {0xA0, 0x81, 0x85, 0x22}, "0x1105");
}

TEST(GoldenNode, SleepFraction) {
    expectRequest("/sleep.fraction", "/sleep.fraction 50", true, {0x60, 0x14, 0x32});
    expectRequest("/sleep.fraction", "/sleep.fraction 0", true, {0x60, 0x14, 0x00});
    expectReply("/sleep.fraction", "/sleep.fraction 50", Fields(), {0xA0, 0x00},
                "Sleep fraction set to 50 %, restarting");
    EXPECT_FALSE(isCanonicalLine("/sleep.fraction 050", "/sleep.fraction",
                                 firmwareDeclaration("/sleep.fraction").args));
    expectError("/sleep.fraction", TypedResult(128), {0xA0, 0x80}, "");
}

TEST(GoldenNode, Power) {
    expectRequest("/power", "/power", false, {0x20, 0x16});
    // AXP2101 on USB with a battery at 4100 mV and 87 %, charger in constant voltage.
    const uint8_t axp2101[] = {0x20, 0x00, 0x04, 0x01, 0x05, 0x00, 0x0B};
    Fields v12;
    v12.setU(0, 2).setU(1, 1).setU(2, 1).setU(3, 4100).setI(4, 87).setU(5, 0).setU(6, 3);
    v12.setBytes(7, axp2101, sizeof(axp2101));
    expectReply("/power", "/power", v12,
                {0xA0, 0x00, 0x02, 0x02, 0x02, 0x85, 0x20, 0xAF, 0x01, 0x01, 0x04, 0x20, 0x00, 0x04,
                 0x01, 0x05, 0x00, 0x0B},
                "pmu=AXP2101 vbus=yes battery=yes 4100mV 87% charging=no state=CV "
                "reg=2000040105000b");
    // AXP192 on battery. The chip gives no percentage (-1) and has no charger state.
    const uint8_t axp192[] = {0xC0, 0x70, 0xD0};
    Fields v11;
    v11.setU(0, 1).setU(1, 0).setU(2, 1).setU(3, 3300).setI(4, -1).setU(5, 0);
    v11.setBytes(7, axp192, sizeof(axp192));
    expectReply("/power", "/power", v11,
                {0xA0, 0x00, 0x01, 0x01, 0x02, 0xE5, 0x19, 0x02, 0x01, 0x00, 0xC0, 0x70, 0xD0},
                "pmu=AXP192 vbus=no battery=yes 3300mV -1% charging=no reg=c070d0");
    // A board without the chip.
    Fields none;
    none.setU(0, 0);
    expectReply("/power", "/power", none, {0xA0, 0x00, 0x00}, "pmu=none");
}

TEST(GoldenNode, Sensor) {
    expectRequest("/sensor", "/sensor", false, {0x20, 0x18});
    // BMP280 at 0x76: 21.50 C and 1013.25 hPa.
    expectReply("/sensor", "/sensor", sensorReading(false, 0x76, 2150, 101325, 0),
                {0xA0, 0x00, 0x00, 0x76, 0xCC, 0x21, 0xCD, 0x97, 0x06},
                "BMP280 at 0x76: 21.50 C 1013.25 hPa");
    // BME280 at 0x77 with 45.50 %RH.
    expectReply("/sensor", "/sensor", sensorReading(true, 0x77, 2150, 101325, 4550),
                {0xA0, 0x00, 0x01, 0x77, 0xCC, 0x21, 0xCD, 0x97, 0x06, 0xC7, 0x23},
                "BME280 at 0x77: 21.50 C 45.50 %RH 1013.25 hPa");
    // A reading kept from 75 s ago.
    Fields stale = sensorReading(false, 0x76, 2150, 101325, 0);
    stale.setU(5, 75);
    expectReply("/sensor", "/sensor", stale,
                {0xA0, 0x00, 0x00, 0x76, 0xCC, 0x21, 0xCD, 0x97, 0x06, 0x00, 0x4C},
                "BMP280 at 0x76: 21.50 C 1013.25 hPa, read 75 s ago, the sensor does not answer "
                "now");
    expectError("/sensor", TypedResult(128), {0xA0, 0x80}, "");
    expectError("/sensor", TypedResult(129), {0xA0, 0x81}, "");
}

TEST(GoldenNode, SensorTextOverTheRangeOfTheSensor) {
    const TypedDeclaration declaration = firmwareDeclaration("/sensor");
    struct Case {
        Fields out;
        const char* text;
    };
    Fields staleNow = sensorReading(true, 0x77, 2150, 101325, 4550);
    staleNow.setU(5, 0);
    for (const Case& c : std::vector<Case>{
             {sensorReading(true, 0x76, -5, 30000, 0),
              "BME280 at 0x76: -0.05 C 0.00 %RH 300.00 hPa"},
             {sensorReading(true, 0x76, -1234, 110007, 10000),
              "BME280 at 0x76: -12.34 C 100.00 %RH 1100.07 hPa"},
             {sensorReading(false, 0x77, 0, 99999, 0), "BMP280 at 0x77: 0.00 C 999.99 hPa"},
             {staleNow, "BME280 at 0x77: 21.50 C 45.50 %RH 1013.25 hPa, read 0 s ago, the sensor "
                        "does not answer now"},
         }) {
        EXPECT_EQ(textOf(declaration, Fields(), replyFrame(declaration, TypedResult(), c.out)).text,
                  c.text);
    }
    // /sensor takes no argument.
    EXPECT_EQ(runTypedFromText(
                  declaration, [](const Fields&, Fields&) { return TypedResult(); }, "now")
                  .status,
              static_cast<uint8_t>(CommandStatus::ARG));
}

TEST(GoldenNode, Identify) {
    expectRequest("/identify", "/identify", false, {0x20, 0x1A});
    expectRequest("/identify", "/identify 30", false, {0x20, 0x1A, 0x1F});
    expectRequest("/identify", "/identify 600", false, {0x20, 0x1A, 0xD9, 0x04});
    Fields out;
    out.setU(0, 10);
    expectReply("/identify", "/identify", out, {0xA0, 0x00, 0x0A}, "Blinking for 10 s");
    out.setU(0, 30);
    expectReply("/identify", "/identify 30", out, {0xA0, 0x00, 0x1E}, "Blinking for 30 s");
    expectError("/identify", TypedResult(128), {0xA0, 0x80}, "");
}

// --- OTA ---

TEST(GoldenOta, Key) {
    expectRequest("/key", "/key", false, {0x20, 0x30});
    Fields none;
    none.setU(0, 0);
    expectReply("/key", "/key", none, {0xA0, 0x00, 0x01}, "No deployment key");
    const uint8_t fingerprint[] = {0xA1, 0xB2, 0xC3, 0xD4};
    Fields stored;
    stored.setBytes(1, fingerprint, sizeof(fingerprint));
    expectReply("/key", "/key", stored, {0xA0, 0x00, 0x00, 0xA1, 0xB2, 0xC3, 0xD4},
                "Deployment key stored, fingerprint a1b2c3d4");
}

TEST(GoldenOta, MaintOpen) {
    expectRequest("/maint.open", "/maint.open 600", true, {0x60, 0x32, 0xD8, 0x04});
    expectRequest("/maint.open", "/maint.open 300 ap", true, {0x60, 0x32, 0xAC, 0x02, 0x01});
    Fields in;
    const TypedDeclaration declaration = firmwareDeclaration("/maint.open");
    for (const char* bad : {"", "ap", "600 pull", "600 ap now", "-1"}) {
        EXPECT_FALSE(parseArgs(declaration.args, bad, in)) << bad;
    }

    // A pull window, the second in 24 h.
    Fields pull;
    pull.setU(1, 600).setU(3, 2);
    expectReply("/maint.open", "/maint.open 600", pull, {0xA0, 0x00, 0x00, 0xD9, 0x04, 0x00, 0x03},
                "Pull window open for 600 s (2 of 8 in 24 h)");
    // The access point of node 1A2B.
    Fields accessPoint;
    accessPoint.setU(2, 300).setU(3, 1).setText(5, "LM-1A2B");
    expectReply("/maint.open", "/maint.open 300 ap", accessPoint,
                {0xA0, 0x00, 0x00, 0x00, 0xAD, 0x02, 0x02, 0x00, 'L', 'M', '-', '1', 'A', '2', 'B'},
                "Access point LM-1A2B open for 300 s, upload at http://192.168.4.1/ (1 of 8 in 24 "
                "h)");
    // The open window got a new end.
    Fields extended;
    extended.setU(0, 540);
    expectReply("/maint.open", "/maint.open 600", extended, {0xA0, 0x00, 0x9D, 0x04},
                "Window extended, 540 s left");
    // A gateway checks at once over its own WiFi.
    Fields gateway;
    gateway.setU(4, 0);
    expectReply("/maint.open", "/maint.open 600", gateway,
                {0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
                "Checking the OTA server over the gateway WiFi");

    expectError("/maint.open", TypedResult(128), {0xA0, 0x80}, "");
    // The policy says what was too low, and the limit of the day comes as a number.
    expectError("/maint.open", TypedResult(131, 0), {0xA0, 0x83, 0x00}, "REFUSE_BATTERY");
    expectError("/maint.open", TypedResult(131, 1), {0xA0, 0x83, 0x01}, "REFUSE_HEAP");
    expectError("/maint.open", TypedResult(132, 8), {0xA0, 0x84, 0x08}, "8");
    expectError("/maint.open", TypedResult(136), {0xA0, 0x88}, "");
}

TEST(GoldenOta, MaintClose) {
    expectRequest("/maint.close", "/maint.close", true, {0x60, 0x33});
    Fields none;
    none.setU(0, 0);
    expectReply("/maint.close", "/maint.close", none, {0xA0, 0x00, 0x01}, "No window open");
    Fields closing;
    closing.setU(1, 0);
    expectReply("/maint.close", "/maint.close", closing, {0xA0, 0x00, 0x00, 0x01},
                "Closing the window");
}

TEST(GoldenOta, Version) {
    expectRequest("/version", "/version", false, {0x20, 0x34});
    // 0.1.2 built from commit 2bfdeeb1, in slot app0, valid, bootloader 20f487, board tbeam.
    expectReply(
        "/version", "/version", versionReply(false),
        {0xA0, 0x00, 0x00, 0x01, 0x02, 0x01, 0x00, 0x20, 0xF4, 0x87, 0x01, 0x2B, 0xFD, 0xEE, 0xB1},
        "0.1.2+g2bfdeeb1 app0 VALID bl=20f487 env=tbeam");
    // The same commit with changes that were not committed.
    expectReply("/version", "/version", versionReply(true),
                {0xA0, 0x00, 0x00, 0x01, 0x02, 0x01, 0x00, 0x20, 0xF4, 0x87, 0x01, 0x2B, 0xFD, 0xEE,
                 0xB1, 0x01},
                "0.1.2+g2bfdeeb1.dirty app0 VALID bl=20f487 env=tbeam");
    // A new image on the other slot of a debug build.
    const uint8_t bootloader[] = {0x20, 0xF4, 0x87};
    const uint8_t commit[] = {0x00, 0xAB, 0xCD, 0x0F};
    Fields pending;
    pending.setVersion(0, 1, 20, 255).setU(1, 1).setU(2, 1).setBytes(3, bootloader, 3).setU(4, 1);
    pending.setBytes(5, commit, 4);
    expectReply(
        "/version", "/version", pending,
        {0xA0, 0x00, 0x01, 0x14, 0xFF, 0x02, 0x01, 0x20, 0xF4, 0x87, 0x02, 0x00, 0xAB, 0xCD, 0x0F},
        "1.20.255+g00abcd0f app1 PENDING_VERIFY bl=20f487 env=tbeam-debug");
    // A slot and a board this firmware has no name for.
    Fields unknown;
    unknown.setVersion(0, 0, 1, 2).setU(2, 0).setBytes(3, bootloader, 3).setBytes(5, commit, 4);
    expectReply(
        "/version", "/version", unknown,
        {0xA0, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x20, 0xF4, 0x87, 0x00, 0x00, 0xAB, 0xCD, 0x0F},
        "0.1.2+g00abcd0f VALID bl=20f487");
}

TEST(GoldenOta, OtaStatus) {
    expectRequest("/ota.status", "/ota.status", false, {0x20, 0x35});
    Fields valid;
    valid.setU(0, 1).setU(1, 0).setU(2, 0).setU(3, 0).setU(4, 0);
    expectReply("/ota.status", "/ota.status", valid, {0xA0, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00},
                "last=VALID pending=no reason=0 unexplained=0 blacklisted=0");
    Fields rolledBack;
    rolledBack.setU(0, 2).setU(1, 1).setU(2, 2).setU(3, 1).setU(4, 3);
    expectReply("/ota.status", "/ota.status", rolledBack,
                {0xA0, 0x00, 0x03, 0x01, 0x02, 0x01, 0x03},
                "last=ROLLED_BACK pending=yes reason=2 unexplained=1 blacklisted=3");
    Fields never;
    never.setU(0, 0).setU(1, 0).setU(2, 0).setU(3, 0).setU(4, 0);
    expectReply("/ota.status", "/ota.status", never, {0xA0, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00},
                "last=NONE pending=no reason=0 unexplained=0 blacklisted=0");
}

TEST(GoldenOta, Reboot) {
    expectRequest("/reboot", "/reboot", true, {0x60, 0x36});
    expectReply("/reboot", "/reboot", Fields(), {0xA0, 0x00}, "Rebooting");
    expectError("/reboot", TypedResult(128), {0xA0, 0x80}, "");
}

TEST(GoldenOta, MaintWifi) {
    // The sealed credentials travel as they are. The signed line holds them in lowercase hex.
    expectRequest("/maint.wifi", "/maint.wifi deadbeef0102", true,
                  {0x60, 0x38, 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02});
    const TypedDeclaration declaration = firmwareDeclaration("/maint.wifi");
    EXPECT_FALSE(isCanonicalLine("/maint.wifi DEADBEEF0102", "/maint.wifi", declaration.args));
    Fields in;
    for (const char* bad : {"", "dead beef", "abc", "home secret12"}) {
        EXPECT_FALSE(parseArgs(declaration.args, bad, in)) << bad;
    }

    Fields sensor;
    sensor.setText(1, "home");
    expectReply("/maint.wifi", "/maint.wifi deadbeef0102", sensor,
                {0xA0, 0x00, 0x00, 'h', 'o', 'm', 'e'}, "WiFi credentials stored for home");
    Fields gateway;
    gateway.setU(0, 0).setText(1, "home");
    expectReply("/maint.wifi", "/maint.wifi deadbeef0102", gateway,
                {0xA0, 0x00, 0x01, 'h', 'o', 'm', 'e'},
                "WiFi credentials stored for home, reconnecting");
    expectError("/maint.wifi", TypedResult(128), {0xA0, 0x80}, "");
    expectError("/maint.wifi", TypedResult(129), {0xA0, 0x81}, "");
    // ESP_ERR_WIFI_NOT_INIT is 0x3001.
    expectError("/maint.wifi", TypedResult(130, 0x3001), {0xA0, 0x82, 0x81, 0x60}, "0x3001");
}

TEST(GoldenOta, MaintWifiCarriesEightySevenCharacters) {
    const TypedDeclaration declaration = firmwareDeclaration("/maint.wifi");
    // The sealed bytes are the SSID, one separator and the password.
    constexpr size_t SIGNED_OVERHEAD = BinaryCommandFrame::HEADER_SIZE + 4 + sizeof(CommandTag);
    const Bytes sealed(FRAME_MAX_BYTES - SIGNED_OVERHEAD, 0x5A);
    EXPECT_EQ(sealed.size(), 88U);
    BinaryCommandFrame frame;
    frame.id = declaration.id;
    frame.isSigned = true;
    Fields in;
    in.setBytes(0, sealed.data(), sealed.size());
    ASSERT_TRUE(encodeFields(declaration.args, in, frame.payload));
    EXPECT_EQ(frame.encode().size(), FRAME_MAX_BYTES);
    // The reply names the longest SSID.
    Fields out;
    out.setU(0, 0).setText(1, std::string(32, 'n'));
    EXPECT_EQ(replyFrame(declaration, TypedResult(), out).size(), 35U);
}

// --- WiFi ---

TEST(GoldenWifi, ConnectWiFi) {
    expectRequest("/connectWiFi", "/connectWiFi", true, {0x60, 0x50});
    expectReply("/connectWiFi", "/connectWiFi", Fields(), {0xA0, 0x00}, "Connected");
    expectError("/connectWiFi", TypedResult(128), {0xA0, 0x80}, "");
}

TEST(GoldenWifi, GetIP) {
    expectRequest("/getIP", "/getIP", false, {0x20, 0x51});
    Fields out;
    out.setU(0, 192).setU(1, 168).setU(2, 1).setU(3, 5);
    expectReply("/getIP", "/getIP", out, {0xA0, 0x00, 0xC0, 0x01, 0xA8, 0x01, 0x01, 0x05},
                "192.168.1.5");
    out.setU(0, 10).setU(1, 0).setU(2, 0).setU(3, 7);
    expectReply("/getIP", "/getIP", out, {0xA0, 0x00, 0x0A, 0x00, 0x00, 0x07}, "10.0.0.7");
    out.setU(0, 255).setU(1, 255).setU(2, 255).setU(3, 255);
    expectReply("/getIP", "/getIP", out,
                {0xA0, 0x00, 0xFF, 0x01, 0xFF, 0x01, 0xFF, 0x01, 0xFF, 0x01}, "255.255.255.255");
    expectError("/getIP", TypedResult(128), {0xA0, 0x80}, "");
}

TEST(GoldenWifi, ResetWiFiData) {
    expectRequest("/resetWiFiData", "/resetWiFiData", true, {0x60, 0x52});
    expectReply("/resetWiFiData", "/resetWiFiData", Fields(), {0xA0, 0x00},
                "WiFi data reset successfully");
    expectError("/resetWiFiData", TypedResult(128, 0x3001), {0xA0, 0x80, 0x81, 0x60}, "0x3001");
}

// --- Display ---

TEST(GoldenDisplay, TheCommandsWithoutArguments) {
    struct Case {
        const char* name;
        uint8_t id;
        const char* text;
    };
    for (const Case& c : std::vector<Case>{
             {"/displayOn", 0x60, "Display On"},
             {"/displayOff", 0x61, "Display Off"},
             {"/displayBlink", 0x62, "Display Blink"},
             {"/displayClear", 0x63, "Display Clear"},
             {"/displayLogo", 0x64, "Display Logo"},
         }) {
        expectRequest(c.name, c.name, false, {0x20, c.id});
        expectReply(c.name, c.name, Fields(), {0xA0, 0x00}, c.text);
        // The node has no working display.
        expectError(c.name, TypedResult(128), {0xA0, 0x80}, "");
        // A binary frame carries no destination, and a text line has none outside the console.
        Fields in;
        EXPECT_FALSE(parseArgs(firmwareDeclaration(c.name).args, "1A2B", in)) << c.name;
    }
}

TEST(GoldenDisplay, DisplayText) {
    expectRequest("/displayText", "/displayText hi", false, {0x20, 0x65, 'h', 'i'});
    // UTF-8 text travels as it is, with its spaces.
    expectRequest("/displayText", "/displayText Ol\xC3\xA1 1A2B  ok", false,
                  {0x20, 0x65, 'O', 'l', 0xC3, 0xA1, ' ', '1', 'A', '2', 'B', ' ', ' ', 'o', 'k'});
    expectReply("/displayText", "/displayText hi", Fields(), {0xA0, 0x00}, "Display Text");
    expectError("/displayText", TypedResult(128), {0xA0, 0x80}, "");
    const TypedDeclaration declaration = firmwareDeclaration("/displayText");
    Fields in;
    EXPECT_FALSE(parseArgs(declaration.args, "", in));
    // A first word of four hex digits is text.
    ASSERT_TRUE(parseArgs(declaration.args, "1A2B hi", in));
    EXPECT_EQ(in.text(0), "1A2B hi");
    // One frame holds 100 bytes of text.
    BinaryCommandFrame frame;
    frame.id = declaration.id;
    in.setText(0, std::string(100, 'x'));
    ASSERT_TRUE(encodeFields(declaration.args, in, frame.payload));
    EXPECT_EQ(frame.encode().size(), FRAME_MAX_BYTES);
}

// --- Mesh and monitor ---

TEST(GoldenMesh, GetRT) {
    expectRequest("/getRT", "/getRT", false, {0x20, 0x70});
    // A neighbour heard at -92 dBm and 7.25 dB, and a node two hops away behind it.
    Fields out;
    out.setU(0, 2);
    out.addRow(routeRow(0x1A2B, 0x1A2B, 200, 1, -92, 725));
    out.addRow(routeRow(0x3C4D, 0x1A2B, 100, 2, 0, 0));
    expectReply("/getRT", "/getRT", out,
                {0xA0, 0x00, 0x02, 0x1A, 0x2B, 0x1A, 0x2B, 0xC8, 0x01, 0x01, 0x5D,
                 0xAB, 0x0B, 0x3C, 0x4D, 0x1A, 0x2B, 0x64, 0x02, 0x00, 0x00},
                "Routing table, 2 routes: 1a2b (hops:1 lq:200 rssi:-92 snr:7.25) - Via: 1a2b, 3c4d "
                "(hops:2 lq:100) - Via: 1a2b");
    // A weak neighbour below the noise.
    Fields weak;
    weak.setU(0, 1);
    weak.addRow(routeRow(0x0A0B, 0x0A0B, 12, 1, -127, -350));
    expectReply("/getRT", "/getRT", weak,
                {0xA0, 0x00, 0x01, 0x0A, 0x0B, 0x0A, 0x0B, 0x0C, 0x01, 0x80, 0x01, 0xBC, 0x05},
                "Routing table, 1 routes: 0a0b (hops:1 lq:12 rssi:-127 snr:-3.50) - Via: 0a0b");
    Fields empty;
    empty.setU(0, 0);
    expectReply("/getRT", "/getRT", empty, {0xA0, 0x00, 0x00}, "Routing table, 0 routes");
}

TEST(GoldenMesh, GetRTIsCutAtAWholeRouteWhenItPassesOneFrame) {
    const TypedDeclaration declaration = firmwareDeclaration("/getRT");
    // Twenty routes of the largest row, 11 bytes: a strong neighbour with a weak signal reading.
    Fields largest;
    largest.setU(0, 20);
    for (uint16_t i = 0; i < 20; i++) {
        largest.addRow(routeRow(0x1000 + i, 0x1000 + i, 255, 1, -127, -3200));
    }
    Bytes payload;
    size_t kept = 0;
    ASSERT_TRUE(encodeTypedReplyWithin(declaration, TypedResult(), largest, REPLY_FIELDS_MAX_BYTES,
                                       payload, &kept));
    EXPECT_EQ(kept, 9U);
    EXPECT_EQ(payload.size(), 100U);
    Fields decoded;
    TypedResult result;
    ASSERT_TRUE(decodeTypedReply(declaration, 0, payload.data(), payload.size(), result, decoded));
    EXPECT_EQ(decoded.u(0), 20U);
    EXPECT_EQ(decoded.rowCount(), 9U);
    std::string text;
    ASSERT_TRUE(renderTemplate(declaration.replyTemplate, declaration.reply, decoded,
                               declaration.args, Fields(), text));
    EXPECT_EQ(text.compare(0, 26, "Routing table, 20 routes: "), 0) << text;
    EXPECT_EQ(text.substr(text.size() - 53),
              "1008 (hops:1 lq:255 rssi:-127 snr:-32.00) - Via: 1008");

    // Twenty routes of the smallest row, 8 bytes: far nodes with a low link quality.
    Fields smallest;
    smallest.setU(0, 20);
    for (uint16_t i = 0; i < 20; i++) smallest.addRow(routeRow(0x2000 + i, 0x1000, 100, 3, 0, 0));
    payload.clear();
    ASSERT_TRUE(encodeTypedReplyWithin(declaration, TypedResult(), smallest, REPLY_FIELDS_MAX_BYTES,
                                       payload, &kept));
    EXPECT_EQ(kept, 12U);
    EXPECT_EQ(payload.size(), 97U);
}

TEST(GoldenMonitor, Install) {
    expectRequest("/install", "/install", true, {0x60, 0x78});
    expectRequest("/install", "/install 0", true, {0x60, 0x78, 0x01});
    expectRequest("/install", "/install 900", true, {0x60, 0x78, 0x85, 0x07});
    Fields on;
    on.setU(0, 600);
    expectReply("/install", "/install", on, {0xA0, 0x00, 0xD9, 0x04}, "OK install 600 s");
    on.setU(0, 900);
    expectReply("/install", "/install 900", on, {0xA0, 0x00, 0x85, 0x07}, "OK install 900 s");
    Fields off;
    off.setU(1, 0);
    expectReply("/install", "/install 0", off, {0xA0, 0x00, 0x00, 0x01}, "OK install off");
}

TEST(GoldenMonitor, Report) {
    expectRequest("/report", "/report", false, {0x20, 0x79});
    Fields usual;
    usual.setU(0, 300).setU(1, 0).setU(2, 212);
    expectReply("/report", "/report", usual, {0xA0, 0x00, 0xAC, 0x02, 0x00, 0xD4, 0x01},
                "report every 300 s (default), next in 212 s");
    Fields stored;
    stored.setU(0, 600).setU(1, 1).setU(2, 1);
    expectReply("/report", "/report", stored, {0xA0, 0x00, 0xD8, 0x04, 0x01, 0x01},
                "report every 600 s (stored), next in 1 s");
    // Install mode shortens a stored interval of 600 s.
    Fields install;
    install.setU(0, 20).setU(1, 1).setU(2, 12).setU(3, 600);
    expectReply("/report", "/report", install, {0xA0, 0x00, 0x14, 0x01, 0x0C, 0xD9, 0x04},
                "report every 20 s (install mode, then 600 s stored), next in 12 s");
    install.setU(1, 0).setU(3, 300);
    expectReply("/report", "/report", install, {0xA0, 0x00, 0x14, 0x00, 0x0C, 0xAD, 0x02},
                "report every 20 s (install mode, then 300 s default), next in 12 s");
}

TEST(GoldenMonitor, ReportSet) {
    expectRequest("/report.set", "/report.set 600", true, {0x60, 0x7A, 0xD8, 0x04});
    // 0 is the canonical way to go back to the default.
    expectRequest("/report.set", "/report.set 0", true, {0x60, 0x7A, 0x00});
    const TypedDeclaration declaration = firmwareDeclaration("/report.set");
    EXPECT_FALSE(isCanonicalLine("/report.set 0600", "/report.set", declaration.args));
    // The word is for the serial console, which turns it into 0 before the arguments are read.
    Fields in;
    EXPECT_FALSE(parseArgs(declaration.args, "default", in));

    Fields stored;
    stored.setU(0, 600);
    expectReply("/report.set", "/report.set 600", stored, {0xA0, 0x00, 0xD9, 0x04},
                "Report interval set to 600 s");
    Fields toDefault;
    toDefault.setU(1, 300);
    expectReply("/report.set", "/report.set 0", toDefault, {0xA0, 0x00, 0x00, 0xAD, 0x02},
                "Report interval set to default (300 s)");
    expectError("/report.set", TypedResult(128, 0x1105), {0xA0, 0x80, 0x85, 0x22}, "0x1105");
    expectError("/report.set", CommandStatus::ARG, {0xA0, 0x05}, "");
}

// --- Every command ---

TEST(GoldenCommands, EveryCommandWithAnIdHasVectorsHere) {
    // The names the tests above cover. A new command with an id gets its vectors first.
    const std::vector<std::string> covered = {
        "/role",         "/role.set",     "/sleep",         "/sleep.set",   "/sleep.fraction",
        "/power",        "/sensor",       "/identify",      "/key",         "/maint.open",
        "/maint.close",  "/version",      "/ota.status",    "/reboot",      "/maint.wifi",
        "/connectWiFi",  "/getIP",        "/resetWiFiData", "/displayOn",   "/displayOff",
        "/displayBlink", "/displayClear", "/displayLogo",   "/displayText", "/getRT",
        "/install",      "/report",       "/report.set"};
    const CommandCatalogue catalogue = firmwareCatalogue();
    size_t withId = 0;
    for (unsigned id = 1; id <= 255; id++) {
        const CommandSpec* found = catalogue.findById(static_cast<uint8_t>(id));
        if (found == nullptr) continue;
        withId++;
        bool listed = false;
        for (const std::string& name : covered) listed = listed || name == found->name;
        EXPECT_TRUE(listed) << found->name;
    }
    EXPECT_EQ(withId, covered.size());
}

TEST(GoldenCommands, NoTemplateNeedsTheTextHandler) {
    // A request without optional arguments renders with empty reply fields or fails cleanly,
    // and every template is valid for its schemas.
    const CommandCatalogue catalogue = firmwareCatalogue();
    for (unsigned id = 1; id <= 255; id++) {
        const CommandSpec* found = catalogue.findById(static_cast<uint8_t>(id));
        if (found == nullptr) continue;
        TypedDeclaration declaration;
        ASSERT_TRUE(typedDeclarationOf(*found, declaration)) << found->name;
        EXPECT_TRUE(checkTemplate(declaration.replyTemplate, declaration.reply, declaration.args))
            << found->name;
        // The line of /cmds holds the three texts as they were declared.
        EXPECT_EQ(CommandCatalogue::field(found->replyTemplate), found->replyTemplate);
        EXPECT_EQ(CommandCatalogue::field(found->reply), found->reply);
        EXPECT_EQ(CommandCatalogue::field(found->args), found->args);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
