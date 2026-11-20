#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "reportInterval.h"

namespace {

constexpr uint32_t DEFAULT_MS = 300000;

}  // namespace

TEST(ReportInterval, KnowsTheValuesAnOperatorMayStore) {
    EXPECT_FALSE(reportIntervalValid(REPORT_INTERVAL_NONE));
    EXPECT_FALSE(reportIntervalValid(59));
    EXPECT_TRUE(reportIntervalValid(60));
    EXPECT_TRUE(reportIntervalValid(43200));
    EXPECT_FALSE(reportIntervalValid(86401));
    EXPECT_FALSE(reportIntervalValid(0xFFFFFFFF));
}

TEST(ReportInterval, TheDefaultIsInForceWhenNothingIsStored) {
    ReportPacing pacing = chooseReportPacing(DEFAULT_MS, REPORT_INTERVAL_NONE, false, 20000);
    EXPECT_EQ(pacing.configuredMs, DEFAULT_MS);
    EXPECT_EQ(pacing.intervalMs, DEFAULT_MS);
    EXPECT_FALSE(pacing.stored);
    EXPECT_FALSE(pacing.installShortens);
}

TEST(ReportInterval, TheStoredIntervalReplacesTheDefault) {
    ReportPacing pacing = chooseReportPacing(DEFAULT_MS, 900, false, 20000);
    EXPECT_EQ(pacing.configuredMs, 900000U);
    EXPECT_EQ(pacing.intervalMs, 900000U);
    EXPECT_TRUE(pacing.stored);
    EXPECT_FALSE(pacing.installShortens);

    pacing = chooseReportPacing(DEFAULT_MS, 43200, false, 20000);
    EXPECT_EQ(pacing.intervalMs, 43200000U);
}

TEST(ReportInterval, AStoredValueOutsideTheRangeSelectsTheDefault) {
    for (uint32_t storedS : {1U, 59U, 86401U, 0xFFFFFFFFU}) {
        ReportPacing pacing = chooseReportPacing(DEFAULT_MS, storedS, false, 20000);
        EXPECT_EQ(pacing.intervalMs, DEFAULT_MS) << storedS;
        EXPECT_FALSE(pacing.stored) << storedS;
    }
}

TEST(ReportInterval, InstallModeShortensTheInterval) {
    ReportPacing pacing = chooseReportPacing(DEFAULT_MS, REPORT_INTERVAL_NONE, true, 20000);
    EXPECT_EQ(pacing.configuredMs, DEFAULT_MS);
    EXPECT_EQ(pacing.intervalMs, 20000U);
    EXPECT_FALSE(pacing.stored);
    EXPECT_TRUE(pacing.installShortens);

    pacing = chooseReportPacing(DEFAULT_MS, 3600, true, 153900);
    EXPECT_EQ(pacing.configuredMs, 3600000U);
    EXPECT_EQ(pacing.intervalMs, 153900U);
    EXPECT_TRUE(pacing.stored);
    EXPECT_TRUE(pacing.installShortens);
}

TEST(ReportInterval, InstallModeNeverLengthensTheInterval) {
    ReportPacing pacing = chooseReportPacing(DEFAULT_MS, 60, true, 153900);
    EXPECT_EQ(pacing.intervalMs, 60000U);
    EXPECT_TRUE(pacing.stored);
    EXPECT_FALSE(pacing.installShortens);

    pacing = chooseReportPacing(DEFAULT_MS, 60, true, 60000);
    EXPECT_EQ(pacing.intervalMs, 60000U);
    EXPECT_FALSE(pacing.installShortens);
}

TEST(ReportInterval, DescribesTheIntervalInForce) {
    EXPECT_EQ(describeReportPacing(chooseReportPacing(DEFAULT_MS, 0, false, 20000), 212),
              "report every 300 s (default), next in 212 s");
    EXPECT_EQ(describeReportPacing(chooseReportPacing(DEFAULT_MS, 900, false, 20000), 0),
              "report every 900 s (stored), next in 0 s");
    EXPECT_EQ(describeReportPacing(chooseReportPacing(DEFAULT_MS, 43200, false, 20000), 43200),
              "report every 43200 s (stored), next in 43200 s");
}

TEST(ReportInterval, DescribesInstallMode) {
    EXPECT_EQ(describeReportPacing(chooseReportPacing(DEFAULT_MS, 0, true, 20000), 12),
              "report every 20 s (install mode, then 300 s default), next in 12 s");
    EXPECT_EQ(describeReportPacing(chooseReportPacing(DEFAULT_MS, 3600, true, 153900), 150),
              "report every 153 s (install mode, then 3600 s stored), next in 150 s");
    // Install mode that does not shorten the interval is not mentioned.
    EXPECT_EQ(describeReportPacing(chooseReportPacing(DEFAULT_MS, 60, true, 153900), 5),
              "report every 60 s (stored), next in 5 s");
}

TEST(ReportInterval, TheLongestReplyIsShort) {
    ReportPacing pacing = chooseReportPacing(DEFAULT_MS, 43200, true, 7199999);
    EXPECT_LE(describeReportPacing(pacing, 43200).size(), 80U);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
