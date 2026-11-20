#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "monReport.h"

namespace {

MonReport sampleReport() {
    MonReport report;
    report.uptimeMs = 0x01020304;
    report.txQueue = 5;
    report.rxQueue = 0x0607;
    report.routes.push_back({0x1A2B, 0x3C4D, 200, 1, -97, -29});
    report.routes.push_back({0x0010, 0x0020, 7, 3, 0, 0});
    return report;
}

MonReport sampleReportV2() {
    MonReport report = sampleReport();
    report.uptimeMs = 0x01020304ULL * 1000 + 999;
    report.rxQueue = 6;
    report.intervalS = 300;
    report.battMv = 3987;
    report.externalPower = true;
    report.batteryPresent = true;
    return report;
}

MonReport withSensor(MonReport report) {
    report.hasSensor = true;
    report.sensor.tempCentiC = -1234;
    report.sensor.rhCentiPercent = 5550;
    report.sensor.pressureDeciHpa = 10132;
    return report;
}

MonReport withRoutes(size_t count) {
    MonReport report = sampleReportV2();
    report.routes.clear();
    for (size_t i = 0; i < count; i++) {
        report.routes.push_back({static_cast<uint16_t>(i), static_cast<uint16_t>(i), 1, 1, -50, 8});
    }
    return report;
}

/** Payload of a T-Beam at SF9: a 115 byte packet less the mesh and LoRaChat headers. */
constexpr size_t SF9_PAYLOAD = 102;

/** Copies @p bytes into a heap block of exactly their size, so ASan sees any read past the end. */
std::unique_ptr<uint8_t[]> exactCopy(const std::vector<uint8_t>& bytes) {
    std::unique_ptr<uint8_t[]> copy(new uint8_t[bytes.size()]);
    std::copy(bytes.begin(), bytes.end(), copy.get());
    return copy;
}

}  // namespace

TEST(MonReportV1, EncodesTheLegacyPackedLayout) {
    const std::vector<uint8_t> expected = {
        0xFF, 0xFF,                      // marker
        0x04, 0x03, 0x02, 0x01,          // uptime
        0x05, 0x00,                      // TX queue
        0x07, 0x06,                      // RX queue
        0x02, 0x00, 0x00, 0x00,          // route count
        0x2B, 0x1A, 0x4D, 0x3C, 200, 1,  // first route
        0x10, 0x00, 0x20, 0x00, 7,   3,  // second route
    };
    EXPECT_EQ(encodeMonReportV1(sampleReport()), expected);
}

TEST(MonReportV1, RoundTrips) {
    std::vector<uint8_t> bytes = encodeMonReportV1(sampleReport());
    MonReport decoded = sampleReportV2();
    ASSERT_TRUE(decodeMonReport(bytes.data(), bytes.size(), decoded));
    EXPECT_EQ(decoded.version, 1);
    EXPECT_EQ(decoded.uptimeMs, 0x01020304u);
    EXPECT_EQ(monUptimeMs(decoded), 0x01020304u);
    EXPECT_EQ(decoded.txQueue, 5);
    EXPECT_EQ(decoded.rxQueue, 0x0607);
    ASSERT_EQ(decoded.routes.size(), 2u);
    EXPECT_EQ(decoded.routes[0].neighbor, 0x1A2B);
    EXPECT_EQ(decoded.routes[0].nextHop, 0x3C4D);
    EXPECT_EQ(decoded.routes[0].linkQuality, 200);
    EXPECT_EQ(decoded.routes[1].hopCount, 3);
}

TEST(MonReportV1, DecodeLeavesTheNewFieldsUnknown) {
    std::vector<uint8_t> bytes = encodeMonReportV1(sampleReport());
    MonReport decoded = withSensor(sampleReportV2());
    ASSERT_TRUE(decodeMonReport(bytes.data(), bytes.size(), decoded));
    EXPECT_EQ(decoded.intervalS, 0);
    EXPECT_EQ(decoded.battMv, 0);
    EXPECT_FALSE(decoded.externalPower);
    EXPECT_FALSE(decoded.batteryPresent);
    EXPECT_FALSE(decoded.hasSensor);
    EXPECT_EQ(decoded.routes[0].rssi, 0);
    EXPECT_EQ(decoded.routes[0].snrQuarterDb, 0);
}

TEST(MonReportV1, DecodesAReportWithoutRoutes) {
    MonReport report = sampleReport();
    report.routes.clear();
    std::vector<uint8_t> bytes = encodeMonReportV1(report);
    ASSERT_EQ(bytes.size(), MON_REPORT_HEADER_SIZE_V1);
    MonReport decoded;
    EXPECT_TRUE(decodeMonReport(bytes.data(), bytes.size(), decoded));
    EXPECT_TRUE(decoded.routes.empty());
}

TEST(MonReportV1, DecodeRejectsCountBeyondPayload) {
    std::vector<uint8_t> bytes = encodeMonReportV1(sampleReport());
    bytes[10] = 0xFF;  // claims 255 routes, carries 2
    auto payload = exactCopy(bytes);
    MonReport decoded;
    EXPECT_FALSE(decodeMonReport(payload.get(), bytes.size(), decoded));
}

TEST(MonReportV1, DecodeRejectsHugeCount) {
    std::vector<uint8_t> bytes = encodeMonReportV1(sampleReport());
    bytes[13] = 0x80;  // route count overflows size_t arithmetic on 32-bit targets
    auto payload = exactCopy(bytes);
    MonReport decoded;
    EXPECT_FALSE(decodeMonReport(payload.get(), bytes.size(), decoded));
}

TEST(MonReportV1, DecodeRejectsTrailingBytes) {
    std::vector<uint8_t> bytes = encodeMonReportV1(sampleReport());
    bytes.push_back(0);
    MonReport decoded;
    EXPECT_FALSE(decodeMonReport(bytes.data(), bytes.size(), decoded));
}

TEST(MonReportV1, DecodeRejectsShortHeader) {
    std::vector<uint8_t> bytes = encodeMonReportV1(sampleReport());
    bytes.resize(MON_REPORT_HEADER_SIZE_V1 - 1);
    auto payload = exactCopy(bytes);
    MonReport decoded;
    EXPECT_FALSE(decodeMonReport(payload.get(), bytes.size(), decoded));
    EXPECT_FALSE(decodeMonReport(nullptr, 0, decoded));
}

TEST(MonReport, DecodeRejectsUnknownMarkers) {
    for (const std::vector<uint8_t>& source :
         {encodeMonReportV1(sampleReport()), encodeMonReport(sampleReportV2())}) {
        std::vector<uint8_t> bytes = source;
        bytes[0] = 0x01;
        MonReport decoded;
        EXPECT_FALSE(decodeMonReport(bytes.data(), bytes.size(), decoded));
        bytes[0] = 0xFD;
        EXPECT_FALSE(decodeMonReport(bytes.data(), bytes.size(), decoded));
    }
}

TEST(MonReport, DecodeRejectsPayloadsShorterThanAMarker) {
    const std::vector<uint8_t> oneByte = {0xFE};
    auto payload = exactCopy(oneByte);
    MonReport decoded;
    EXPECT_FALSE(decodeMonReport(payload.get(), 1, decoded));
    EXPECT_FALSE(decodeMonReport(payload.get(), 0, decoded));
}

TEST(MonReportV2, EncodesThePackedLayout) {
    const std::vector<uint8_t> expected = {
        0xFE, 0xFF,                                  // marker
        0x05,                                        // flags: external power, battery present
        0x04, 0x03, 0x02, 0x01,                      // uptime in seconds
        0x05,                                        // TX queue
        0x06,                                        // RX queue
        0x2C, 0x01,                                  // interval in seconds
        0x93, 0x0F,                                  // battery in mV
        0x02,                                        // route count
        0x2B, 0x1A, 0x4D, 0x3C, 200, 1, 0x9F, 0xE3,  // first route, -97 dBm and -7.25 dB
        0x10, 0x00, 0x20, 0x00, 7,   3, 0x00, 0x00,  // second route, signal unknown
    };
    EXPECT_EQ(encodeMonReport(sampleReportV2()), expected);
}

TEST(MonReportV2, EncodesTheSensorBlockAfterTheHeader) {
    std::vector<uint8_t> bytes = encodeMonReport(withSensor(sampleReportV2()));
    ASSERT_EQ(bytes.size(), MON_REPORT_HEADER_SIZE + MON_SENSOR_SIZE + 2 * MON_ROUTE_SIZE);
    EXPECT_EQ(bytes[2], 0x0D);
    const std::vector<uint8_t> block(bytes.begin() + MON_REPORT_HEADER_SIZE,
                                     bytes.begin() + MON_REPORT_HEADER_SIZE + MON_SENSOR_SIZE);
    const std::vector<uint8_t> expected = {0x2E, 0xFB, 0xAE, 0x15, 0x94, 0x27};
    EXPECT_EQ(block, expected);
    EXPECT_EQ(bytes[MON_REPORT_HEADER_SIZE + MON_SENSOR_SIZE], 0x2B);
}

TEST(MonReportV2, RoundTrips) {
    std::vector<uint8_t> bytes = encodeMonReport(sampleReportV2());
    auto payload = exactCopy(bytes);
    MonReport decoded;
    ASSERT_TRUE(decodeMonReport(payload.get(), bytes.size(), decoded));
    EXPECT_EQ(decoded.version, 2);
    EXPECT_EQ(decoded.uptimeMs, 0x01020304ULL * 1000);
    EXPECT_EQ(decoded.txQueue, 5);
    EXPECT_EQ(decoded.rxQueue, 6);
    EXPECT_EQ(decoded.intervalS, 300);
    EXPECT_EQ(decoded.battMv, 3987);
    EXPECT_TRUE(decoded.externalPower);
    EXPECT_FALSE(decoded.charging);
    EXPECT_TRUE(decoded.batteryPresent);
    EXPECT_FALSE(decoded.installMode);
    EXPECT_FALSE(decoded.hasSensor);
    ASSERT_EQ(decoded.routes.size(), 2u);
    EXPECT_EQ(decoded.routes[0].neighbor, 0x1A2B);
    EXPECT_EQ(decoded.routes[0].nextHop, 0x3C4D);
    EXPECT_EQ(decoded.routes[0].linkQuality, 200);
    EXPECT_EQ(decoded.routes[0].hopCount, 1);
    EXPECT_EQ(decoded.routes[0].rssi, -97);
    EXPECT_EQ(decoded.routes[0].snrQuarterDb, -29);
    EXPECT_FLOAT_EQ(monSnrDb(decoded.routes[0].snrQuarterDb), -7.25F);
    EXPECT_EQ(decoded.routes[1].neighbor, 0x0010);
    EXPECT_EQ(decoded.routes[1].rssi, 0);
}

TEST(MonReportV2, RoundTripsEveryFlag) {
    for (int flag = 0; flag < 5; flag++) {
        MonReport report;
        report.externalPower = flag == 0;
        report.charging = flag == 1;
        report.batteryPresent = flag == 2;
        report.installMode = flag == 3;
        report.binaryCommands = flag == 4;
        std::vector<uint8_t> bytes = encodeMonReport(report);
        MonReport decoded;
        ASSERT_TRUE(decodeMonReport(bytes.data(), bytes.size(), decoded));
        EXPECT_EQ(decoded.externalPower, flag == 0);
        EXPECT_EQ(decoded.charging, flag == 1);
        EXPECT_EQ(decoded.batteryPresent, flag == 2);
        EXPECT_EQ(decoded.installMode, flag == 3);
        EXPECT_EQ(decoded.binaryCommands, flag == 4);
    }
    // A node that understands binary command frames says so in bit 5.
    MonReport binary;
    binary.binaryCommands = true;
    EXPECT_EQ(encodeMonReport(binary)[2], 0x20);
    // A format 1 report cannot say it.
    std::vector<uint8_t> old = encodeMonReportV1(binary);
    MonReport fromOld;
    ASSERT_TRUE(decodeMonReport(old.data(), old.size(), fromOld));
    EXPECT_FALSE(fromOld.binaryCommands);
    MonReport install;
    install.installMode = true;
    EXPECT_EQ(encodeMonReport(install)[2], 0x10);
}

TEST(MonReportV2, DecodeIgnoresUnknownFlags) {
    std::vector<uint8_t> bytes = encodeMonReport(sampleReportV2());
    bytes[2] |= 0xE0;
    MonReport decoded;
    ASSERT_TRUE(decodeMonReport(bytes.data(), bytes.size(), decoded));
    EXPECT_TRUE(decoded.externalPower);
    EXPECT_EQ(decoded.routes.size(), 2u);
}

TEST(MonReportV2, RoundTripsTheSensorBlock) {
    std::vector<uint8_t> bytes = encodeMonReport(withSensor(sampleReportV2()));
    auto payload = exactCopy(bytes);
    MonReport decoded;
    ASSERT_TRUE(decodeMonReport(payload.get(), bytes.size(), decoded));
    EXPECT_TRUE(decoded.hasSensor);
    EXPECT_EQ(decoded.sensor.tempCentiC, -1234);
    EXPECT_EQ(decoded.sensor.rhCentiPercent, 5550);
    EXPECT_EQ(decoded.sensor.pressureDeciHpa, 10132);
    ASSERT_EQ(decoded.routes.size(), 2u);
    EXPECT_EQ(decoded.routes[1].hopCount, 3);
}

TEST(MonReportV2, RoundTripsASensorWithoutHumidity) {
    MonReport report = withSensor(sampleReportV2());
    report.sensor.rhCentiPercent = MON_NO_HUMIDITY;
    std::vector<uint8_t> bytes = encodeMonReport(report);
    MonReport decoded;
    ASSERT_TRUE(decodeMonReport(bytes.data(), bytes.size(), decoded));
    EXPECT_EQ(decoded.sensor.rhCentiPercent, 0xFFFF);
}

TEST(MonReportV2, DecodesAReportWithoutRoutes) {
    MonReport report = sampleReportV2();
    report.routes.clear();
    std::vector<uint8_t> bytes = encodeMonReport(report);
    ASSERT_EQ(bytes.size(), MON_REPORT_HEADER_SIZE);
    auto payload = exactCopy(bytes);
    MonReport decoded;
    ASSERT_TRUE(decodeMonReport(payload.get(), bytes.size(), decoded));
    EXPECT_TRUE(decoded.routes.empty());
    EXPECT_EQ(decoded.battMv, 3987);
}

TEST(MonReportV2, DecodeRejectsEveryTruncation) {
    for (const MonReport& report : {sampleReportV2(), withSensor(sampleReportV2())}) {
        std::vector<uint8_t> bytes = encodeMonReport(report);
        for (size_t size = 0; size < bytes.size(); size++) {
            std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + size);
            auto payload = exactCopy(cut);
            MonReport decoded;
            EXPECT_FALSE(decodeMonReport(payload.get(), size, decoded)) << size;
        }
    }
}

TEST(MonReportV2, DecodeRejectsCountThatDoesNotMatchTheSize) {
    std::vector<uint8_t> bytes = encodeMonReport(sampleReportV2());
    for (uint8_t count : {0, 1, 3, 255}) {
        bytes[13] = count;
        auto payload = exactCopy(bytes);
        MonReport decoded;
        EXPECT_FALSE(decodeMonReport(payload.get(), bytes.size(), decoded)) << int(count);
    }
}

TEST(MonReportV2, DecodeRejectsTrailingBytes) {
    std::vector<uint8_t> bytes = encodeMonReport(sampleReportV2());
    bytes.push_back(0);
    MonReport decoded;
    EXPECT_FALSE(decodeMonReport(bytes.data(), bytes.size(), decoded));
}

TEST(MonReportV2, DecodeRejectsASensorFlagWithoutItsBlock) {
    std::vector<uint8_t> bytes = encodeMonReport(sampleReportV2());
    bytes[2] |= MON_FLAG_SENSOR;
    auto payload = exactCopy(bytes);
    MonReport decoded;
    EXPECT_FALSE(decodeMonReport(payload.get(), bytes.size(), decoded));

    bytes = encodeMonReport(withSensor(sampleReportV2()));
    bytes[2] &= static_cast<uint8_t>(~MON_FLAG_SENSOR);
    EXPECT_FALSE(decodeMonReport(bytes.data(), bytes.size(), decoded));
}

TEST(MonReportV2, AFailedDecodeLeavesTheReportUntouched) {
    std::vector<uint8_t> bytes = encodeMonReport(sampleReportV2());
    bytes.pop_back();
    MonReport decoded;
    decoded.battMv = 1234;
    EXPECT_FALSE(decodeMonReport(bytes.data(), bytes.size(), decoded));
    EXPECT_EQ(decoded.battMv, 1234);
}

TEST(MonReportV2, SaturatesQueuesAndUptime) {
    MonReport report;
    report.txQueue = 255;
    report.rxQueue = 256;
    report.uptimeMs = std::numeric_limits<uint64_t>::max();
    std::vector<uint8_t> bytes = encodeMonReport(report);
    MonReport decoded;
    ASSERT_TRUE(decodeMonReport(bytes.data(), bytes.size(), decoded));
    EXPECT_EQ(decoded.txQueue, 255);
    EXPECT_EQ(decoded.rxQueue, 255);
    EXPECT_EQ(decoded.uptimeMs, 0xFFFFFFFFULL * 1000);
}

TEST(MonReportV2, WritesAtMostTheRoutesItsCountHolds) {
    std::vector<uint8_t> bytes = encodeMonReport(withRoutes(300));
    ASSERT_EQ(bytes.size(), MON_REPORT_HEADER_SIZE + 255 * MON_ROUTE_SIZE);
    MonReport decoded;
    ASSERT_TRUE(decodeMonReport(bytes.data(), bytes.size(), decoded));
    EXPECT_EQ(decoded.routes.size(), 255u);
}

TEST(MonReport, UptimeInMillisecondsSaturatesAt32Bits) {
    MonReport report;
    report.uptimeMs = 4294967ULL * 1000;
    EXPECT_EQ(monUptimeMs(report), 4294967000u);
    report.uptimeMs = 4294968ULL * 1000;
    EXPECT_EQ(monUptimeMs(report), 0xFFFFFFFFu);
}

TEST(MonReport, PacksRssi) {
    EXPECT_EQ(monPackRssi(0.0F), 0);
    EXPECT_EQ(monPackRssi(std::nanf("")), 0);
    EXPECT_EQ(monPackRssi(-97.0F), -97);
    EXPECT_EQ(monPackRssi(-97.6F), -98);
    EXPECT_EQ(monPackRssi(-0.2F), -1);
    EXPECT_EQ(monPackRssi(5.0F), -1);
    EXPECT_EQ(monPackRssi(-127.0F), -127);
    EXPECT_EQ(monPackRssi(-140.0F), -127);
    EXPECT_EQ(monPackRssi(-1.0e9F), -127);
}

TEST(MonReport, PacksSnrInQuarterDecibels) {
    EXPECT_EQ(monPackSnr(0.0F), 0);
    EXPECT_EQ(monPackSnr(std::nanf("")), 0);
    EXPECT_EQ(monPackSnr(9.75F), 39);
    EXPECT_EQ(monPackSnr(-7.25F), -29);
    EXPECT_EQ(monPackSnr(-7.3F), -29);
    EXPECT_EQ(monPackSnr(-7.4F), -30);
    EXPECT_EQ(monPackSnr(31.75F), 127);
    EXPECT_EQ(monPackSnr(40.0F), 127);
    EXPECT_EQ(monPackSnr(-32.0F), -128);
    EXPECT_EQ(monPackSnr(-1.0e9F), -128);
    EXPECT_FLOAT_EQ(monSnrDb(39), 9.75F);
    EXPECT_FLOAT_EQ(monSnrDb(-128), -32.0F);
    EXPECT_FLOAT_EQ(monSnrDb(0), 0.0F);
}

TEST(MonReport, RoutesPerMessageFitsOneFrame) {
    EXPECT_EQ(monRoutesPerMessage(SF9_PAYLOAD, false), 11u);
    EXPECT_EQ(monRoutesPerMessage(SF9_PAYLOAD, true), 10u);
    EXPECT_EQ(monRoutesPerMessage(MON_REPORT_HEADER_SIZE + MON_ROUTE_SIZE, false), 1u);
    EXPECT_EQ(monRoutesPerMessage(MON_REPORT_HEADER_SIZE + MON_ROUTE_SIZE - 1, false), 0u);
    EXPECT_EQ(monRoutesPerMessage(MON_REPORT_HEADER_SIZE + MON_ROUTE_SIZE, true), 0u);
    EXPECT_EQ(monRoutesPerMessage(MON_REPORT_HEADER_SIZE + MON_SENSOR_SIZE + MON_ROUTE_SIZE, true),
              1u);
    EXPECT_EQ(monRoutesPerMessage(0, false), 0u);
    EXPECT_EQ(monRoutesPerMessage(100000, false), 255u);
}

TEST(MonReportSplit, AReportWithoutRoutesIsOneMessage) {
    std::vector<MonReport> parts = splitMonReport(withRoutes(0), SF9_PAYLOAD);
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_TRUE(parts[0].routes.empty());
    EXPECT_EQ(parts[0].battMv, 3987);
    EXPECT_EQ(splitMonReport(withRoutes(0), MON_REPORT_HEADER_SIZE).size(), 1u);
}

TEST(MonReportSplit, EveryPartFitsAndRepeatsTheState) {
    std::vector<MonReport> parts = splitMonReport(withRoutes(25), SF9_PAYLOAD);
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0].routes.size(), 11u);
    EXPECT_EQ(parts[1].routes.size(), 11u);
    EXPECT_EQ(parts[2].routes.size(), 3u);
    EXPECT_EQ(parts[2].routes[2].neighbor, 24);
    for (const MonReport& part : parts) {
        EXPECT_LE(encodeMonReport(part).size(), SF9_PAYLOAD);
        EXPECT_EQ(part.battMv, 3987);
        EXPECT_EQ(part.intervalS, 300);
        EXPECT_TRUE(part.externalPower);
    }
    EXPECT_EQ(splitMonReport(withRoutes(11), SF9_PAYLOAD).size(), 1u);
    EXPECT_EQ(splitMonReport(withRoutes(12), SF9_PAYLOAD).size(), 2u);
}

TEST(MonReportSplit, OnlyTheFirstPartCarriesTheSensorBlock) {
    std::vector<MonReport> parts = splitMonReport(withSensor(withRoutes(22)), SF9_PAYLOAD);
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_TRUE(parts[0].hasSensor);
    EXPECT_EQ(parts[0].sensor.tempCentiC, -1234);
    EXPECT_EQ(parts[0].routes.size(), 10u);
    EXPECT_FALSE(parts[1].hasSensor);
    EXPECT_EQ(parts[1].routes.size(), 11u);
    EXPECT_FALSE(parts[2].hasSensor);
    EXPECT_EQ(parts[2].routes.size(), 1u);
    EXPECT_EQ(parts[2].routes[0].neighbor, 21);
    for (const MonReport& part : parts) EXPECT_LE(encodeMonReport(part).size(), SF9_PAYLOAD);
}

TEST(MonReportSplit, ASensorBlockThatLeavesNoRoomSendsTheRoutesInTheNextPart) {
    size_t payload = MON_REPORT_HEADER_SIZE + MON_ROUTE_SIZE;
    std::vector<MonReport> parts = splitMonReport(withSensor(withRoutes(2)), payload);
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_TRUE(parts[0].hasSensor);
    EXPECT_TRUE(parts[0].routes.empty());
    EXPECT_EQ(parts[1].routes.size(), 1u);
    EXPECT_EQ(parts[2].routes.size(), 1u);
    for (const MonReport& part : parts) EXPECT_LE(encodeMonReport(part).size(), payload);
}

TEST(MonReportSplit, GivesNothingWhenThePayloadIsTooSmall) {
    EXPECT_TRUE(splitMonReport(withRoutes(0), MON_REPORT_HEADER_SIZE - 1).empty());
    EXPECT_TRUE(splitMonReport(withRoutes(1), MON_REPORT_HEADER_SIZE + MON_ROUTE_SIZE - 1).empty());
    EXPECT_TRUE(splitMonReport(withSensor(withRoutes(0)), MON_REPORT_HEADER_SIZE).empty());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
