#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "monReport.h"

namespace {

MonReport sampleReport() {
    MonReport report;
    report.uptimeMs = 0x01020304;
    report.txQueue = 5;
    report.rxQueue = 0x0607;
    report.routes.push_back({0x1A2B, 0x3C4D, 200, 1});
    report.routes.push_back({0x0010, 0x0020, 7, 3});
    return report;
}

/** Copies @p bytes into a heap block of exactly their size, so ASan sees any read past the end. */
std::unique_ptr<uint8_t[]> exactCopy(const std::vector<uint8_t>& bytes) {
    std::unique_ptr<uint8_t[]> copy(new uint8_t[bytes.size()]);
    std::copy(bytes.begin(), bytes.end(), copy.get());
    return copy;
}

}  // namespace

TEST(MonReport, EncodesTheLegacyPackedLayout) {
    const std::vector<uint8_t> expected = {
        0xFF, 0xFF,                      // marker
        0x04, 0x03, 0x02, 0x01,          // uptime
        0x05, 0x00,                      // TX queue
        0x07, 0x06,                      // RX queue
        0x02, 0x00, 0x00, 0x00,          // route count
        0x2B, 0x1A, 0x4D, 0x3C, 200, 1,  // first route
        0x10, 0x00, 0x20, 0x00, 7,   3,  // second route
    };
    EXPECT_EQ(encodeMonReport(sampleReport()), expected);
}

TEST(MonReport, RoundTrips) {
    std::vector<uint8_t> bytes = encodeMonReport(sampleReport());
    MonReport decoded;
    ASSERT_TRUE(decodeMonReport(bytes.data(), bytes.size(), decoded));
    EXPECT_EQ(decoded.uptimeMs, 0x01020304u);
    EXPECT_EQ(decoded.txQueue, 5);
    EXPECT_EQ(decoded.rxQueue, 0x0607);
    ASSERT_EQ(decoded.routes.size(), 2u);
    EXPECT_EQ(decoded.routes[0].neighbor, 0x1A2B);
    EXPECT_EQ(decoded.routes[0].nextHop, 0x3C4D);
    EXPECT_EQ(decoded.routes[0].linkQuality, 200);
    EXPECT_EQ(decoded.routes[1].hopCount, 3);
}

TEST(MonReport, DecodesAReportWithoutRoutes) {
    MonReport report = sampleReport();
    report.routes.clear();
    std::vector<uint8_t> bytes = encodeMonReport(report);
    ASSERT_EQ(bytes.size(), MON_REPORT_HEADER_SIZE);
    MonReport decoded;
    EXPECT_TRUE(decodeMonReport(bytes.data(), bytes.size(), decoded));
    EXPECT_TRUE(decoded.routes.empty());
}

TEST(MonReport, DecodeRejectsCountBeyondPayload) {
    std::vector<uint8_t> bytes = encodeMonReport(sampleReport());
    bytes[10] = 0xFF;  // claims 255 routes, carries 2
    auto payload = exactCopy(bytes);
    MonReport decoded;
    EXPECT_FALSE(decodeMonReport(payload.get(), bytes.size(), decoded));
}

TEST(MonReport, DecodeRejectsHugeCount) {
    std::vector<uint8_t> bytes = encodeMonReport(sampleReport());
    bytes[13] = 0x80;  // route count overflows size_t arithmetic on 32-bit targets
    auto payload = exactCopy(bytes);
    MonReport decoded;
    EXPECT_FALSE(decodeMonReport(payload.get(), bytes.size(), decoded));
}

TEST(MonReport, DecodeRejectsTrailingBytes) {
    std::vector<uint8_t> bytes = encodeMonReport(sampleReport());
    bytes.push_back(0);
    MonReport decoded;
    EXPECT_FALSE(decodeMonReport(bytes.data(), bytes.size(), decoded));
}

TEST(MonReport, DecodeRejectsShortHeader) {
    std::vector<uint8_t> bytes = encodeMonReport(sampleReport());
    bytes.resize(MON_REPORT_HEADER_SIZE - 1);
    auto payload = exactCopy(bytes);
    MonReport decoded;
    EXPECT_FALSE(decodeMonReport(payload.get(), bytes.size(), decoded));
    EXPECT_FALSE(decodeMonReport(nullptr, 0, decoded));
}

TEST(MonReport, DecodeRejectsMissingMarker) {
    std::vector<uint8_t> bytes = encodeMonReport(sampleReport());
    bytes[0] = 0x01;
    MonReport decoded;
    EXPECT_FALSE(decodeMonReport(bytes.data(), bytes.size(), decoded));
}

TEST(MonReport, RoutesPerMessageFitsOneFrame) {
    EXPECT_EQ(monRoutesPerMessage(102), 14u);
    EXPECT_EQ(monRoutesPerMessage(MON_REPORT_HEADER_SIZE + MON_ROUTE_SIZE), 1u);
    EXPECT_EQ(monRoutesPerMessage(MON_REPORT_HEADER_SIZE + MON_ROUTE_SIZE - 1), 0u);
    EXPECT_EQ(monRoutesPerMessage(0), 0u);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
