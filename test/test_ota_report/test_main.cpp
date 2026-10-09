#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "otaReport.h"
#include "reportAuth.h"
#include "reportVectors.h"

namespace {

const ReportVector& vector(const char* name) {
    for (const ReportVector& v : REPORT_VECTORS) {
        if (std::strcmp(v.name, name) == 0) return v;
    }
    throw std::runtime_error(name);
}

DeploymentKey vectorKey() {
    DeploymentKey key;
    std::memcpy(key.data(), REPORT_VECTOR_KEY, key.size());
    return key;
}

}  // namespace

// The vector bodies come from LoRaMesherWeb ota/vectors/make_vectors.py, the server side.
TEST(OtaReport, EncodesTheNoopVectorByteForByte) {
    OtaReport r;
    r.node = 0x7680;
    r.mac = "08:3a:f2:aa:bb:cc";
    r.env = "tbeam";
    r.role = "sensor";
    r.ver = "0.1.0+g4434b51";
    r.part = "app0";
    r.state = "VALID";
    r.boot = 12;
    r.rr = "POWERON";
    r.bl = "f43854";
    r.event = ReportEvent::NOOP;
    r.decision = 1;
    r.rssi = -77;
    r.heapFree = 213000;
    r.heapMin = 202000;
    r.battMv = 4110;
    r.vbus = 1;
    r.uptimeS = 9;
    EXPECT_EQ(encodeReport(r), vector("noop").body);
}

TEST(OtaReport, EncodesTheWrittenVectorByteForByte) {
    OtaReport r;
    r.node = 0x7680;
    r.env = "tbeam";
    r.ver = "0.1.0+g4434b51";
    r.part = "app0";
    r.state = "VALID";
    r.boot = 12;
    r.event = ReportEvent::WRITTEN;
    r.status = 0;
    r.sha = "3fa2c1d4e5f60718";
    r.rssi = -79;
    r.dlMs = 23600;
    r.bytes = 1212336;
    EXPECT_EQ(encodeReport(r), vector("written").body);
}

TEST(OtaReport, EncodesTheRollbackVectorByteForByte) {
    OtaReport r;
    r.node = 0x5E9C;
    r.env = "tbeam";
    r.ver = "0.1.0+g4434b51";
    r.part = "app1";
    r.state = "VALID";
    r.boot = 4;
    r.rr = "SW";
    r.event = ReportEvent::ROLLBACK;
    r.lastInvalid = "app0";
    r.reason = 5;
    r.sha = "0011223344556677";
    EXPECT_EQ(encodeReport(r), vector("rollback").body);
}

TEST(OtaReport, LeavesOutAbsentFieldsAndKeepsTheRequiredOnes) {
    OtaReport r;
    r.node = 0x000C;
    r.env = "tbeam";
    r.ver = "1.0.0";
    r.event = ReportEvent::VALID;
    EXPECT_EQ(encodeReport(r),
              R"({"v":1,"node":"000C","env":"tbeam","ver":"1.0.0","boot":0,"event":"valid"})");
}

TEST(OtaReport, EscapesQuotesBackslashesAndControlCharacters) {
    OtaReport r;
    r.env = "a\"b\\c";
    r.ver = std::string("x\ny") + '\x01';
    r.event = ReportEvent::FAILED;
    EXPECT_EQ(encodeReport(r), "{\"v\":1,\"node\":\"0000\",\"env\":\"a\\\"b\\\\c\","
                               "\"ver\":\"x\\u000ay\\u0001\",\"boot\":0,\"event\":\"failed\"}");
}

TEST(OtaReport, NamesEveryEvent) {
    EXPECT_STREQ(reportEventName(ReportEvent::NOOP), "noop");
    EXPECT_STREQ(reportEventName(ReportEvent::WRITTEN), "written");
    EXPECT_STREQ(reportEventName(ReportEvent::VALID), "valid");
    EXPECT_STREQ(reportEventName(ReportEvent::ROLLBACK), "rollback");
    EXPECT_STREQ(reportEventName(ReportEvent::FAILED), "failed");
}

TEST(ReportAuth, DerivesTheVectorReportKey) {
    Sha256 kr = reportKey(vectorKey());
    EXPECT_EQ(0, std::memcmp(kr.data(), REPORT_VECTOR_REPORT_KEY, kr.size()));
}

TEST(ReportAuth, SignsEveryValidVectorWithItsTag) {
    Sha256 kr = reportKey(vectorKey());
    for (const ReportVector& v : REPORT_VECTORS) {
        if (v.valid) {
            EXPECT_EQ(reportTag(kr, v.body), v.tag) << v.name;
        } else {
            EXPECT_NE(reportTag(kr, v.body), v.tag) << v.name;
        }
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
