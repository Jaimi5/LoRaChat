#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "deploymentKey.h"
#include "otaReport.h"
#include "otaRecord.h"
#include "reportVectors.h"

namespace {

DeploymentKey vectorKey() {
    DeploymentKey key;
    for (size_t i = 0; i < key.size(); i++) key[i] = static_cast<uint8_t>(i);
    return key;
}

std::string hex(const Sha256& digest) {
    static const char DIGITS[] = "0123456789abcdef";
    std::string text;
    for (uint8_t b : digest) {
        text += DIGITS[b >> 4];
        text += DIGITS[b & 0xF];
    }
    return text;
}

}  // namespace

// The vectors come from LoRaMesherWeb ota/vectors/report_vectors.json (the server's tests).
TEST(OtaReport, BuildsTheServerNoopVector) {
    std::string body = ReportBuilder()
                           .addText("node", "7680")
                           .addText("mac", "08:3a:f2:aa:bb:cc")
                           .addText("env", "tbeam")
                           .addText("role", "sensor")
                           .addText("ver", "0.1.0+g4434b51")
                           .addText("part", "app0")
                           .addText("state", "VALID")
                           .addNumber("boot", 12)
                           .addText("rr", "POWERON")
                           .addText("bl", "f43854")
                           .addText("event", reportEventName(ReportEvent::NOOP))
                           .addNumber("decision", 1)
                           .addNumber("rssi", -77)
                           .addNumber("heap_free", 213000)
                           .addNumber("heap_min", 202000)
                           .addNumber("batt_mv", 4110)
                           .addFlag("vbus", true)
                           .addNumber("uptime_s", 9)
                           .body();
    EXPECT_EQ(body, REPORT_NOOP_BODY);
}

TEST(OtaReport, BuildsTheServerRollbackVector) {
    std::string body = ReportBuilder()
                           .addText("node", "5E9C")
                           .addText("env", "tbeam")
                           .addText("ver", "0.1.0+g4434b51")
                           .addText("part", "app1")
                           .addText("state", "VALID")
                           .addNumber("boot", 4)
                           .addText("rr", "SW")
                           .addText("event", reportEventName(ReportEvent::ROLLBACK))
                           .addText("last_invalid", "app0")
                           .addNumber("reason", 5)
                           .addText("sha", "0011223344556677")
                           .body();
    EXPECT_EQ(body, REPORT_ROLLBACK_BODY);
}

TEST(OtaReport, SignsLikeTheServer) {
    Sha256 kr = reportKey(vectorKey());
    EXPECT_EQ(hex(kr), REPORT_KEY_HEX);
    EXPECT_EQ(reportTag(kr, REPORT_NOOP_BODY), REPORT_NOOP_TAG);
    EXPECT_NE(reportTag(kr, std::string(REPORT_NOOP_BODY) + " "), REPORT_NOOP_TAG);
}

TEST(OtaReport, NamesEveryEvent) {
    EXPECT_STREQ(reportEventName(ReportEvent::NOOP), "noop");
    EXPECT_STREQ(reportEventName(ReportEvent::WRITTEN), "written");
    EXPECT_STREQ(reportEventName(ReportEvent::VALID), "valid");
    EXPECT_STREQ(reportEventName(ReportEvent::ROLLBACK), "rollback");
    EXPECT_STREQ(reportEventName(ReportEvent::FAILED), "failed");
}

TEST(OtaReport, RefusesTextTheServerWouldReject) {
    EXPECT_EQ(ReportBuilder().addText("ver", "").body(), "");
    EXPECT_EQ(ReportBuilder().addText("ver", "a\"b").body(), "");
    EXPECT_EQ(ReportBuilder().addText("ver", "a\\b").body(), "");
    EXPECT_EQ(ReportBuilder().addText("ver", "a b").body(), "");
    EXPECT_EQ(ReportBuilder().addText("ver", std::string(49, 'x')).body(), "");
    EXPECT_EQ(ReportBuilder().addText("ver", std::string(48, 'x')).body(),
              "{\"v\":1,\"ver\":\"" + std::string(48, 'x') + "\"}");
}

TEST(OtaReport, RefusesABodyOverTheLimit) {
    ReportBuilder builder;
    for (int i = 0; i < 40; i++) builder.addText("field", std::string(40, 'x'));
    EXPECT_EQ(builder.body(), "");
}

namespace {

OtaRecord rolledBack(uint8_t seed, uint8_t unexplained) {
    OtaRecord record;
    record.attemptSha.fill(seed);
    record.lastOutcome = OtaOutcome::ROLLED_BACK;
    record.unexplainedRollbacks = unexplained;
    return record;
}

}  // namespace

TEST(VerdictId, SameVerdictHasTheSameId) {
    EXPECT_EQ(verdictId(rolledBack(0xA0, 1)), verdictId(rolledBack(0xA0, 1)));
}

TEST(VerdictId, EachRollbackOfTheSameImageIsANewVerdict) {
    EXPECT_NE(verdictId(rolledBack(0xA0, 1)), verdictId(rolledBack(0xA0, 2)));
}

TEST(VerdictId, ImageAndOutcomeChangeTheId) {
    OtaRecord valid = rolledBack(0xA0, 0);
    valid.lastOutcome = OtaOutcome::VALID;
    EXPECT_NE(verdictId(rolledBack(0xA0, 0)), verdictId(valid));
    EXPECT_NE(verdictId(rolledBack(0xA0, 1)), verdictId(rolledBack(0xB0, 1)));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
