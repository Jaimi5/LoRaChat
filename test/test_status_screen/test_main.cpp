#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "linkQuality.h"
#include "statusScreen.h"

namespace {

StatusNeighbour neighbour(uint16_t address, int16_t rssi, float snr, uint32_t age) {
    StatusNeighbour n;
    n.address = address;
    n.signalKnown = true;
    n.rssiDbm = rssi;
    n.snrDb = snr;
    n.ageSeconds = age;
    return n;
}

void setVersion(StatusSnapshot& s, const char* version) {
    std::snprintf(s.version, sizeof(s.version), "%s", version);
}

void setText(StatusSnapshot& s, const char* text) {
    std::snprintf(s.text, sizeof(s.text), "%s", text);
}

void addThreeNeighbours(StatusSnapshot& s) {
    statusAddNeighbour(s, neighbour(0x0D07, -118, -11.5F, 40));
    statusAddNeighbour(s, neighbour(0x0B12, -97, 6.5F, 4));
    statusAddNeighbour(s, neighbour(0x0C40, -112, -8.0F, 12));
}

StatusSnapshot joinedSensor() {
    StatusSnapshot s;
    s.address = 0x0A3F;
    s.meshState = MeshState::NORMAL_OPERATION;
    s.stateSeconds = 500;
    setVersion(s, "0.1.3+gabc1234");
    s.powerKnown = true;
    s.batteryPresent = true;
    s.batteryMv = 3980;
    statusNoteHeard(s, 12);
    s.syncKnown = true;
    s.hasRoute = true;
    s.nextHop = 0x0B12;
    s.gatewayHops = 2;
    s.nextHopSnrKnown = true;
    s.nextHopSnrDb = 6.5F;
    addThreeNeighbours(s);
    s.connectedNodes = 5;
    s.hasTemperature = true;
    s.temperatureC = 21.4F;
    s.hasHumidity = true;
    s.humidityPercent = 61.0F;
    s.uptimeSeconds = 500;
    return s;
}

StatusSnapshot gateway() {
    StatusSnapshot s;
    s.gateway = true;
    s.address = 0x0A00;
    s.meshState = MeshState::NETWORK_MANAGER;
    setVersion(s, "0.1.3+gabc1234");
    s.powerKnown = true;
    s.externalPower = true;
    statusNoteHeard(s, 3);
    addThreeNeighbours(s);
    s.connectedNodes = 5;
    s.wifiConnected = true;
    s.wifiRssiDbm = -61;
    s.mqttConnected = true;
    s.uptimeSeconds = 3 * 86400 + 100;
    return s;
}

ScreenModel build(const StatusSnapshot& s) {
    ScreenModel model;
    std::memset(&model, 0xAA, sizeof(model));
    buildStatusScreen(s, model);
    return model;
}

char iconChar(const ScreenModel& model) {
    switch (model.icon) {
        case ScreenIcon::JOINED:
            return '~';
        case ScreenIcon::SEARCHING:
            return '?';
        case ScreenIcon::JOINING:
            return '>';
        case ScreenIcon::FAULT:
            return 'X';
        case ScreenIcon::GATEWAY:
            return 'G';
        case ScreenIcon::INSTALL:
            return '!';
    }
    return ' ';
}

/** The screen as text, one line per row. */
std::string grid(const ScreenModel& model) {
    std::string text = std::string(1, iconChar(model)) + " " + model.header + "\n";
    text += std::string(model.big) + "\n";
    for (const auto& row : model.rows) text += std::string(row) + "\n";
    return text;
}

std::string grid(const StatusSnapshot& s) {
    return grid(build(s));
}

void expectWithinWidth(const ScreenModel& model) {
    auto check = [](const char* text, size_t width) {
        size_t length = strnlen(text, width + 1);
        ASSERT_LE(length, width) << text;
        for (size_t i = 0; i < length; i++) {
            ASSERT_GE(text[i], ' ');
            ASSERT_LE(text[i], '~');
        }
    };
    check(model.header, SCREEN_HEADER_CHARS);
    check(model.big, SCREEN_BIG_CHARS);
    for (const auto& row : model.rows) check(row, SCREEN_ROW_CHARS);
    EXPECT_LT(model.iconFrame, SCREEN_SEARCH_FRAMES);
}

}  // namespace

TEST(LinkQuality, ThresholdsAtTheirBoundaries) {
    EXPECT_EQ(verdictFromSnr(true, 10.0F), Verdict::GOOD);
    EXPECT_EQ(verdictFromSnr(true, -2.0F), Verdict::GOOD);
    EXPECT_EQ(verdictFromSnr(true, -2.25F), Verdict::FAIR);
    EXPECT_EQ(verdictFromSnr(true, -7.0F), Verdict::FAIR);
    EXPECT_EQ(verdictFromSnr(true, -7.25F), Verdict::POOR);
    EXPECT_EQ(verdictFromSnr(true, -20.0F), Verdict::POOR);
}

TEST(LinkQuality, UnknownReadingGivesNone) {
    EXPECT_EQ(verdictFromSnr(false, 10.0F), Verdict::NONE);
    EXPECT_STREQ(verdictName(Verdict::NONE), "NONE");
    EXPECT_STREQ(verdictName(Verdict::POOR), "POOR");
    EXPECT_STREQ(verdictName(Verdict::FAIR), "FAIR");
    EXPECT_STREQ(verdictName(Verdict::GOOD), "GOOD");
}

TEST(FormatAge, EveryUnitBoundary) {
    struct {
        uint32_t seconds;
        const char* text;
    } cases[] = {{0, "0s"},          {59, "59s"},
                 {60, "1m"},         {59 * 60 + 59, "59m"},
                 {60 * 60, "1h"},    {23 * 3600 + 3599, "23h"},
                 {24 * 3600, "1d"},  {99 * 86400, "99d"},
                 {0xFFFFFFFF, "99d"}};
    for (const auto& c : cases) {
        char age[5];
        formatAge(age, c.seconds, 99);
        EXPECT_STREQ(age, c.text) << c.seconds;
    }
    char age[5];
    formatAge(age, 120 * 86400, 999);
    EXPECT_STREQ(age, "120d");
    formatAge(age, 0xFFFFFFFF, 999);
    EXPECT_STREQ(age, "999d");
    formatAge(age, 0xFFFFFFFF, 0xFFFFFFFF);
    EXPECT_STREQ(age, "999d");
}

TEST(StatusNeighbours, KeepsTheThreeStrongestInOrder) {
    StatusSnapshot s;
    statusAddNeighbour(s, neighbour(1, -120, 0, 0));
    statusAddNeighbour(s, neighbour(2, -90, 0, 0));
    StatusNeighbour unknown;
    unknown.address = 3;
    statusAddNeighbour(s, unknown);
    statusAddNeighbour(s, neighbour(4, -100, 0, 0));
    statusAddNeighbour(s, neighbour(5, -130, 0, 0));
    ASSERT_EQ(s.neighbourCount, 3);
    EXPECT_EQ(s.neighbours[0].address, 2);
    EXPECT_EQ(s.neighbours[1].address, 4);
    EXPECT_EQ(s.neighbours[2].address, 1);
}

TEST(StatusNeighbours, UnknownSignalComesLast) {
    StatusSnapshot s;
    StatusNeighbour unknown;
    unknown.address = 3;
    statusAddNeighbour(s, unknown);
    statusAddNeighbour(s, neighbour(1, -130, 0, 0));
    ASSERT_EQ(s.neighbourCount, 2);
    EXPECT_EQ(s.neighbours[0].address, 1);
    EXPECT_EQ(s.neighbours[1].address, 3);
    EXPECT_STREQ(build(s).rows[2], "0003   --      --  0s");
}

TEST(StatusScreen, SensorJoined) {
    EXPECT_EQ(grid(joinedSensor()), "~ 0A3F JOINED   3.98V\n"
                                    "GOOD   12s\n"
                                    "via 0B12 +6.5  2 hops\n"
                                    "0B12  -97dBm +6.5  4s\n"
                                    "0C40 -112dBm -8.0 12s\n"
                                    "0D07 -118dBm-11.5 40s\n"
                                    "21.4C 61%RH    v0.1.3\n");
}

TEST(StatusScreen, SensorDiscovery) {
    StatusSnapshot s;
    s.address = 0x0A3F;
    s.meshState = MeshState::DISCOVERY;
    s.stateSeconds = 48;
    setVersion(s, "0.1.3+gabc1234");
    s.powerKnown = true;
    s.batteryPresent = true;
    s.batteryMv = 3980;
    statusAddNeighbour(s, neighbour(0x0B12, -104, -3.0F, 6));
    s.uptimeSeconds = 121;
    ScreenModel model = build(s);
    EXPECT_EQ(grid(model), "? 0A3F DISCOVER 3.98V\n"
                           "SEARCH 48s\n"
                           "no sync yet\n"
                           "0B12 -104dBm -3.0  6s\n"
                           "\n"
                           "\n"
                           "up 2m          v0.1.3\n");
    EXPECT_EQ(model.iconFrame, 1);
    EXPECT_FALSE(model.invertHeader);
}

TEST(StatusScreen, SensorInstallMode) {
    StatusSnapshot s = joinedSensor();
    s.installSeconds = 9 * 60 + 42;
    s.nextHopSnrDb = -6.5F;
    s.lastHeardSeconds = 4;
    EXPECT_EQ(grid(s), "! 0A3F INSTALL  3.98V\n"
                       "FAIR    4s\n"
                       "via 0B12 -6.5dB  9:42\n"
                       "0B12  -97dBm +6.5  4s\n"
                       "0C40 -112dBm -8.0 12s\n"
                       "0D07 -118dBm-11.5 40s\n"
                       "21.4C 61%RH    v0.1.3\n");
}

TEST(StatusScreen, GatewayNormal) {
    EXPECT_EQ(grid(gateway()), "G 0A00 GATEWAY    USB\n"
                               "MQTT    3s\n"
                               "wifi -61dBm  nodes  5\n"
                               "0B12  -97dBm +6.5  4s\n"
                               "0C40 -112dBm -8.0 12s\n"
                               "0D07 -118dBm-11.5 40s\n"
                               "up 3d          v0.1.3\n");
}

TEST(StatusScreen, GatewayMqttDown) {
    StatusSnapshot s = gateway();
    s.mqttConnected = false;
    EXPECT_STREQ(build(s).big, "NO MQTT");
    EXPECT_EQ(build(s).icon, ScreenIcon::GATEWAY);
}

TEST(StatusScreen, GatewayWifiDownTakesPriority) {
    StatusSnapshot s = gateway();
    s.wifiConnected = false;
    EXPECT_STREQ(build(s).big, "NO WIFI");
    EXPECT_STREQ(build(s).rows[0], "wifi --      nodes  5");
    s.mqttConnected = false;
    EXPECT_STREQ(build(s).big, "NO WIFI");
}

TEST(StatusScreen, GatewayAgeIsRightAligned) {
    StatusSnapshot s = gateway();
    s.lastHeardSeconds = 12;
    EXPECT_STREQ(build(s).big, "MQTT   12s");
    s.lastHeardSeconds = 59 * 60;
    EXPECT_STREQ(build(s).big, "MQTT   59m");
    s.everHeard = false;
    EXPECT_STREQ(build(s).big, "MQTT    --");
}

TEST(StatusNoteHeard, KeepsTheSmallestAge) {
    StatusSnapshot s;
    EXPECT_FALSE(s.everHeard);
    statusNoteHeard(s, 40);
    EXPECT_TRUE(s.everHeard);
    EXPECT_EQ(s.lastHeardSeconds, 40U);
    statusNoteHeard(s, 90);
    EXPECT_EQ(s.lastHeardSeconds, 40U);
    statusNoteHeard(s, 0);
    EXPECT_EQ(s.lastHeardSeconds, 0U);
    EXPECT_TRUE(s.everHeard);
}

TEST(StatusScreen, SensorThatOnlySendsShowsWhenItHeardANeighbour) {
    StatusSnapshot s;
    s.meshState = MeshState::NORMAL_OPERATION;
    statusAddNeighbour(s, neighbour(0x0B12, -97, 6.5F, 7));
    EXPECT_STREQ(build(s).big, "GOOD    --");
    statusNoteHeard(s, 7);
    EXPECT_STREQ(build(s).big, "GOOD    7s");
}

TEST(StatusScreen, ASpaceAlwaysSeparatesTheWordFromTheAge) {
    // One value for every text formatAge() can produce, plus the largest counter.
    std::vector<uint32_t> ages;
    for (uint32_t v = 0; v < 60; v++) ages.push_back(v);
    for (uint32_t v = 1; v < 60; v++) ages.push_back(v * 60 + 59);
    for (uint32_t v = 1; v < 24; v++) ages.push_back(v * 3600 + 3599);
    for (uint32_t v = 1; v <= 99; v++) ages.push_back(v * 86400);
    ages.push_back(0xFFFFFFFF);
    const std::set<std::string> alone = {"NO WIFI", "NO MQTT", "RECOVER", "ELECTION"};

    for (uint32_t age : ages) {
        char ageText[5];
        formatAge(ageText, age, 99);
        for (int state = 0; state < 7; state++) {
            // Bits: gateway, WiFi, MQTT, heard, install mode.
            for (int bits = 0; bits < 32; bits++) {
                StatusSnapshot s = joinedSensor();
                s.meshState = static_cast<MeshState>(state);
                s.stateSeconds = age;
                s.gateway = bits & 1;
                s.wifiConnected = bits & 2;
                s.mqttConnected = bits & 4;
                s.everHeard = bits & 8;
                s.lastHeardSeconds = age;
                s.installSeconds = (bits & 16) ? 30 : 0;
                std::string big = build(s).big;
                ASSERT_LE(big.size(), SCREEN_BIG_CHARS);
                if (alone.count(big)) continue;
                size_t space = big.rfind(' ');
                ASSERT_NE(space, std::string::npos) << big;
                ASSERT_GT(space, 0U) << big;
                std::string word = big.substr(0, big.find(' '));
                std::string tail = big.substr(space + 1);
                EXPECT_TRUE(tail == ageText || tail == "--") << big;
                EXPECT_EQ(word.find_first_of("0123456789"), std::string::npos) << big;
                EXPECT_EQ(big.size(), SCREEN_BIG_CHARS) << big;
            }
        }
    }
}

TEST(StatusScreen, SensorStateWordsAndIcons) {
    struct {
        MeshState state;
        const char* header;
        const char* big;
        ScreenIcon icon;
        bool inverted;
    } cases[] = {
        {MeshState::INITIALIZING, "0A3F INIT     3.98V", "SEARCH  7s", ScreenIcon::SEARCHING,
         false},
        {MeshState::DISCOVERY, "0A3F DISCOVER 3.98V", "SEARCH  7s", ScreenIcon::SEARCHING, false},
        {MeshState::JOINING, "0A3F JOINING  3.98V", "JOIN    7s", ScreenIcon::JOINING, false},
        {MeshState::NORMAL_OPERATION, "0A3F JOINED   3.98V", "GOOD   12s", ScreenIcon::JOINED,
         false},
        {MeshState::NETWORK_MANAGER, "0A3F MANAGER  3.98V", "GOOD   12s", ScreenIcon::JOINED,
         false},
        {MeshState::FAULT_RECOVERY, "0A3F RECOVER  3.98V", "RECOVER", ScreenIcon::FAULT, true},
        {MeshState::NM_ELECTION, "0A3F ELECTION 3.98V", "ELECTION", ScreenIcon::FAULT, true},
    };
    for (const auto& c : cases) {
        StatusSnapshot s = joinedSensor();
        s.meshState = c.state;
        s.stateSeconds = 7;
        ScreenModel model = build(s);
        EXPECT_STREQ(model.header, c.header);
        EXPECT_STREQ(model.big, c.big);
        EXPECT_EQ(model.icon, c.icon);
        EXPECT_EQ(model.invertHeader, c.inverted);
    }
}

TEST(StatusScreen, GatewayShowsTheStateWordWhenNotHealthy) {
    struct {
        MeshState state;
        const char* header;
        ScreenIcon icon;
        bool inverted;
    } cases[] = {
        {MeshState::INITIALIZING, "0A00 INIT       USB", ScreenIcon::GATEWAY, false},
        {MeshState::DISCOVERY, "0A00 DISCOVER   USB", ScreenIcon::GATEWAY, false},
        {MeshState::JOINING, "0A00 JOINING    USB", ScreenIcon::GATEWAY, false},
        {MeshState::NORMAL_OPERATION, "0A00 GATEWAY    USB", ScreenIcon::GATEWAY, false},
        {MeshState::NETWORK_MANAGER, "0A00 GATEWAY    USB", ScreenIcon::GATEWAY, false},
        {MeshState::FAULT_RECOVERY, "0A00 RECOVER    USB", ScreenIcon::FAULT, true},
        {MeshState::NM_ELECTION, "0A00 ELECTION   USB", ScreenIcon::FAULT, true},
    };
    for (const auto& c : cases) {
        StatusSnapshot s = gateway();
        s.meshState = c.state;
        ScreenModel model = build(s);
        EXPECT_STREQ(model.header, c.header);
        EXPECT_STREQ(model.big, "MQTT    3s");
        EXPECT_EQ(model.icon, c.icon);
        EXPECT_EQ(model.invertHeader, c.inverted);
    }
}

TEST(StatusScreen, SearchIconAdvancesOncePerSecond) {
    StatusSnapshot s;
    s.meshState = MeshState::DISCOVERY;
    for (uint32_t second = 0; second < 9; second++) {
        s.uptimeSeconds = second;
        EXPECT_EQ(build(s).iconFrame, second % 4);
    }
    s.meshState = MeshState::NORMAL_OPERATION;
    EXPECT_EQ(build(s).iconFrame, 0);
}

TEST(StatusScreen, FaultKeepsItsWordInInstallMode) {
    StatusSnapshot s = joinedSensor();
    s.installSeconds = 30;
    s.meshState = MeshState::FAULT_RECOVERY;
    ScreenModel model = build(s);
    EXPECT_STREQ(model.header, "0A3F RECOVER  3.98V");
    EXPECT_EQ(model.icon, ScreenIcon::FAULT);
}

TEST(StatusScreen, VerdictFollowsTheNextHopAtTheThresholds) {
    struct {
        float snr;
        const char* big;
    } cases[] = {{-2.0F, "GOOD   12s"},
                 {-2.25F, "FAIR   12s"},
                 {-7.0F, "FAIR   12s"},
                 {-7.25F, "POOR   12s"}};
    for (const auto& c : cases) {
        StatusSnapshot s = joinedSensor();
        s.nextHopSnrDb = c.snr;
        EXPECT_STREQ(build(s).big, c.big);
    }
}

TEST(StatusScreen, VerdictFallsBackToTheBestNeighbour) {
    StatusSnapshot s = joinedSensor();
    s.hasRoute = false;
    EXPECT_STREQ(build(s).big, "GOOD   12s");
    EXPECT_STREQ(build(s).rows[0], "no route");
    s.syncKnown = false;
    EXPECT_STREQ(build(s).rows[0], "no sync yet");

    StatusSnapshot poor;
    poor.meshState = MeshState::NORMAL_OPERATION;
    statusAddNeighbour(poor, neighbour(1, -118, -11.5F, 1));
    statusAddNeighbour(poor, neighbour(2, -119, -6.0F, 1));
    EXPECT_STREQ(build(poor).big, "FAIR    --");
}

TEST(StatusScreen, NoNeighboursGivesNoneAndBlankRows) {
    StatusSnapshot s;
    s.address = 0x0A3F;
    s.meshState = MeshState::NORMAL_OPERATION;
    ScreenModel model = build(s);
    EXPECT_STREQ(model.big, "NONE    --");
    EXPECT_STREQ(model.rows[1], "");
    EXPECT_STREQ(model.rows[2], "");
    EXPECT_STREQ(model.rows[3], "");
}

TEST(StatusScreen, NextHopWithoutReading) {
    StatusSnapshot s = joinedSensor();
    s.nextHopSnrKnown = false;
    EXPECT_STREQ(build(s).rows[0], "via 0B12 --    2 hops");
}

TEST(StatusScreen, RouteRowShowsTheHopsToTheGateway) {
    StatusSnapshot s = joinedSensor();
    s.gatewayHops = 1;
    EXPECT_STREQ(build(s).rows[0], "via 0B12 +6.5dB 1 hop");
    s.gatewayHops = 0;
    EXPECT_STREQ(build(s).rows[0], "via 0B12 +6.5dB");
    s.gatewayHops = 3;
    s.hasRoute = false;
    EXPECT_STREQ(build(s).rows[0], "no route");
}

TEST(StatusScreen, RouteRowDropsTheUnitWhenFull) {
    StatusSnapshot s = joinedSensor();
    EXPECT_STREQ(build(s).rows[0], "via 0B12 +6.5  2 hops");
    s.nextHopSnrDb = -11.5F;
    EXPECT_STREQ(build(s).rows[0], "via 0B12 -11.5 2 hops");
    s.gatewayHops = 12;
    EXPECT_STREQ(build(s).rows[0], "via 0B12 -11.5 12hops");
    s.nextHopSnrDb = 6.5F;
    EXPECT_STREQ(build(s).rows[0], "via 0B12 +6.5 12 hops");
    s.gatewayHops = 1;
    s.nextHopSnrDb = -11.5F;
    EXPECT_STREQ(build(s).rows[0], "via 0B12 -11.5  1 hop");
}

TEST(StatusScreen, InstallModeShowsTheRemainingTimeInPlaceOfTheHops) {
    StatusSnapshot s = joinedSensor();
    s.installSeconds = 3600;
    EXPECT_STREQ(build(s).rows[0], "via 0B12 +6.5dB 60:00");
    s.installSeconds = 5;
    EXPECT_STREQ(build(s).rows[0], "via 0B12 +6.5dB  0:05");
}

TEST(StatusScreen, PowerField) {
    StatusSnapshot s = joinedSensor();
    s.externalPower = true;
    s.meshState = MeshState::DISCOVERY;
    EXPECT_STREQ(build(s).header, "0A3F DISCOVER+3.98V");
    s.batteryPresent = false;
    EXPECT_STREQ(build(s).header, "0A3F DISCOVER   USB");
    s.externalPower = false;
    EXPECT_STREQ(build(s).header, "0A3F DISCOVER    --");
    s.batteryPresent = true;
    s.powerKnown = false;
    EXPECT_STREQ(build(s).header, "0A3F DISCOVER    --");
    s.powerKnown = true;
    s.batteryMv = 4196;
    EXPECT_STREQ(build(s).header, "0A3F DISCOVER 4.20V");
    s.batteryMv = 0xFFFFFFFF;
    EXPECT_STREQ(build(s).header, "0A3F DISCOVER 9.99V");
}

TEST(StatusScreen, LastRowPriority) {
    StatusSnapshot s = joinedSensor();
    setText(s, "hello field");
    s.textSeconds = 299;
    EXPECT_STREQ(build(s).rows[4], "hello field    v0.1.3");
    s.textSeconds = 300;
    EXPECT_STREQ(build(s).rows[4], "21.4C 61%RH    v0.1.3");
    s.hasTemperature = false;
    s.hasHumidity = false;
    EXPECT_STREQ(build(s).rows[4], "up 8m          v0.1.3");
}

TEST(StatusScreen, LongDisplayTextIsCut) {
    StatusSnapshot s = joinedSensor();
    setText(s, "a very long line of text");
    EXPECT_STREQ(build(s).rows[4], "a very long li v0.1.3");
    setText(s, "tab\there");
    EXPECT_STREQ(build(s).rows[4], "tab?here       v0.1.3");
}

TEST(StatusScreen, SensorReadingPiecesThatFit) {
    StatusSnapshot s = joinedSensor();
    s.temperatureC = -12.34F;
    s.hasPressure = true;
    s.pressureHpa = 1013.2F;
    EXPECT_STREQ(build(s).rows[4], "-12.3C 61%RH   v0.1.3");
    s.hasHumidity = false;
    EXPECT_STREQ(build(s).rows[4], "-12.3C 1013hPa v0.1.3");
    s.hasTemperature = false;
    EXPECT_STREQ(build(s).rows[4], "1013hPa        v0.1.3");
}

TEST(StatusScreen, LongVersionIsShortened) {
    StatusSnapshot s = joinedSensor();
    s.hasTemperature = false;
    s.hasHumidity = false;
    setVersion(s, "10.20.30-rc1-dirty+gabc1234");
    EXPECT_STREQ(build(s).rows[4], "up 8m      v10.20.30-");
    setVersion(s, "v1.0.1");
    EXPECT_STREQ(build(s).rows[4], "up 8m          v1.0.1");
    setVersion(s, "");
    EXPECT_STREQ(build(s).rows[4], "up 8m");
    std::memset(s.version, 'x', sizeof(s.version));
    EXPECT_STREQ(build(s).rows[4], "up 8m      vxxxxxxxxx");
}

TEST(StatusScreen, WidestNeighbourRowFits) {
    StatusSnapshot s;
    statusAddNeighbour(s, neighbour(0xFFFF, -150, -20.5F, 0xFFFFFFFF));
    statusAddNeighbour(s, neighbour(0xFFFE, -32768, -1000.0F, 59));
    EXPECT_STREQ(build(s).rows[1], "FFFF -150dBm-20.5 99d");
    EXPECT_STREQ(build(s).rows[2], "FFFE -199dBm-99.9 59s");
}

TEST(StatusScreen, EqualSnapshotsGiveEqualBytes) {
    ScreenModel a = build(joinedSensor());
    ScreenModel b;
    std::memset(&b, 0x55, sizeof(b));
    buildStatusScreen(joinedSensor(), b);
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(a)), 0);
}

TEST(StatusScreen, RandomSnapshotsStayWithinTheWidth) {
    std::mt19937 random(12345);
    auto pick = [&random](uint32_t limit) { return static_cast<uint32_t>(random() % limit); };
    auto anyNumber = [&random, &pick]() -> uint32_t {
        static const uint32_t EDGES[] = {0, 1, 59, 60, 3599, 3600, 86399, 86400, 0xFFFFFFFF};
        return pick(2) ? EDGES[pick(9)] : static_cast<uint32_t>(random());
    };
    auto anyFloat = [&pick]() -> float {
        static const float EDGES[] = {0.0F, -0.04F, -99.95F, 99.95F, -1e9F, 1e9F, 100.0F, -7.0F};
        return pick(2) ? EDGES[pick(8)] : (static_cast<float>(pick(40000)) - 20000.0F) / 100.0F;
    };
    for (int i = 0; i < 5000; i++) {
        StatusSnapshot s;
        s.gateway = pick(2);
        s.address = static_cast<uint16_t>(random());
        s.meshState = static_cast<MeshState>(pick(7));
        s.stateSeconds = anyNumber();
        for (size_t c = 0; c < pick(sizeof(s.version) + 1); c++) {
            s.version[c] = static_cast<char>(1 + pick(255));
        }
        s.powerKnown = pick(2);
        s.batteryPresent = pick(2);
        s.externalPower = pick(2);
        s.batteryMv = anyNumber();
        s.everHeard = pick(2);
        s.lastHeardSeconds = anyNumber();
        s.syncKnown = pick(2);
        s.gatewayHops = static_cast<uint8_t>(random());
        s.hasRoute = pick(2);
        s.nextHop = static_cast<uint16_t>(random());
        s.nextHopSnrKnown = pick(2);
        s.nextHopSnrDb = anyFloat();
        uint32_t neighbours = pick(6);
        for (uint32_t n = 0; n < neighbours; n++) {
            StatusNeighbour entry =
                neighbour(static_cast<uint16_t>(random()), static_cast<int16_t>(random()),
                          anyFloat(), anyNumber());
            entry.signalKnown = pick(2);
            statusAddNeighbour(s, entry);
        }
        s.connectedNodes = anyNumber();
        s.installSeconds = pick(2) ? anyNumber() : 0;
        s.hasTemperature = pick(2);
        s.temperatureC = anyFloat();
        s.hasHumidity = pick(2);
        s.humidityPercent = anyFloat();
        s.hasPressure = pick(2);
        s.pressureHpa = anyFloat() * 100.0F;
        s.wifiConnected = pick(2);
        s.wifiRssiDbm = static_cast<int16_t>(random());
        s.mqttConnected = pick(2);
        for (size_t c = 0; c < pick(sizeof(s.text)); c++) {
            s.text[c] = static_cast<char>(1 + pick(255));
        }
        s.textSeconds = anyNumber();
        s.uptimeSeconds = anyNumber();
        expectWithinWidth(build(s));
        if (::testing::Test::HasFatalFailure()) return;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (argc > 1 && std::string(argv[1]) == "--print-screens") {
        StatusSnapshot discovery;
        discovery.address = 0x0A3F;
        discovery.meshState = MeshState::DISCOVERY;
        discovery.stateSeconds = 48;
        setVersion(discovery, "0.1.3+gabc1234");
        discovery.powerKnown = true;
        discovery.batteryPresent = true;
        discovery.batteryMv = 3980;
        statusAddNeighbour(discovery, neighbour(0x0B12, -104, -3.0F, 6));
        discovery.uptimeSeconds = 121;
        StatusSnapshot install = joinedSensor();
        install.installSeconds = 9 * 60 + 42;
        install.nextHopSnrDb = -6.5F;
        install.lastHeardSeconds = 4;
        StatusSnapshot mqttDown = gateway();
        mqttDown.mqttConnected = false;
        StatusSnapshot joining = discovery;
        joining.meshState = MeshState::JOINING;
        std::printf("Sensor, joined\n%s\nSensor, discovery\n%s\nSensor, joining\n%s\n"
                    "Sensor, install mode\n%s\nGateway, normal\n%s\nGateway, MQTT down\n%s",
                    grid(joinedSensor()).c_str(), grid(discovery).c_str(), grid(joining).c_str(),
                    grid(install).c_str(), grid(gateway()).c_str(), grid(mqttDown).c_str());
        return 0;
    }
    return RUN_ALL_TESTS();
}
