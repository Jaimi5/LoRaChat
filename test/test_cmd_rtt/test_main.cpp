#include <gtest/gtest.h>

#include <cstdint>

#include "cmdRtt.h"

namespace {

RttPath path(uint16_t dst, uint8_t hops = 2, uint16_t nextHop = 0x0A01,
             uint32_t superframeMs = 15000) {
    RttPath p;
    p.dst = dst;
    p.hops = hops;
    p.nextHop = nextHop;
    p.superframeMs = superframeMs;
    return p;
}

uint32_t srtt(RttEstimator& rtt, const RttPath& p) {
    uint32_t value = 0;
    EXPECT_TRUE(rtt.estimate(p, value));
    return value;
}

}  // namespace

TEST(CmdRtt, ANodeWithoutSamplesHasNoEstimate) {
    RttEstimator rtt;
    uint32_t value = 77;
    EXPECT_FALSE(rtt.estimate(path(0x0B12), value));
    EXPECT_EQ(value, 77U);
    EXPECT_EQ(rtt.size(), 0U);
    EXPECT_EQ(rtt.variationMs(0x0B12), 0U);
}

TEST(CmdRtt, TheFirstSampleSetsTheEstimateAndHalfOfItAsVariation) {
    RttEstimator rtt;
    ASSERT_TRUE(rtt.addSample(path(0x0B12), 24000, 4000, false));
    EXPECT_EQ(srtt(rtt, path(0x0B12)), 20000U);
    EXPECT_EQ(rtt.variationMs(0x0B12), 10000U);
    EXPECT_EQ(rtt.size(), 1U);
}

TEST(CmdRtt, TheSampleLeavesOutTheWaitForTheGatewaySlot) {
    RttEstimator rtt;
    rtt.addSample(path(1), 30000, 12000, false);
    EXPECT_EQ(srtt(rtt, path(1)), 18000U);
    // A wait longer than the whole round trip gives a sample of zero.
    rtt.addSample(path(2), 5000, 9000, false);
    EXPECT_EQ(srtt(rtt, path(2)), 0U);
}

TEST(CmdRtt, LaterSamplesAreSmoothed) {
    RttEstimator rtt;
    rtt.addSample(path(1), 20000, 0, false);
    rtt.addSample(path(1), 28000, 0, false);
    // RTTVAR = 3/4 * 10000 + 1/4 * |20000 - 28000|, SRTT = 7/8 * 20000 + 1/8 * 28000.
    EXPECT_EQ(rtt.variationMs(1), 9500U);
    EXPECT_EQ(srtt(rtt, path(1)), 21000U);
    rtt.addSample(path(1), 13000, 0, false);
    // RTTVAR = 3/4 * 9500 + 1/4 * |21000 - 13000|, SRTT = 7/8 * 21000 + 1/8 * 13000.
    EXPECT_EQ(rtt.variationMs(1), 9125U);
    EXPECT_EQ(srtt(rtt, path(1)), 20000U);
    EXPECT_EQ(rtt.size(), 1U);
}

TEST(CmdRtt, ARetransmittedRequestGivesNoSample) {
    RttEstimator rtt;
    EXPECT_FALSE(rtt.addSample(path(1), 60000, 0, true));
    uint32_t value = 0;
    EXPECT_FALSE(rtt.estimate(path(1), value));
    rtt.addSample(path(1), 20000, 0, false);
    EXPECT_FALSE(rtt.addSample(path(1), 90000, 0, true));
    EXPECT_EQ(srtt(rtt, path(1)), 20000U);
    EXPECT_EQ(rtt.variationMs(1), 10000U);
}

TEST(CmdRtt, AnotherHopCountStartsTheEntryAgain) {
    RttEstimator rtt;
    rtt.addSample(path(1, 2), 20000, 0, false);
    uint32_t value = 0;
    EXPECT_FALSE(rtt.estimate(path(1, 3), value));
    // The entry is gone, also for the old hop count.
    EXPECT_FALSE(rtt.estimate(path(1, 2), value));
    EXPECT_EQ(rtt.size(), 0U);
}

TEST(CmdRtt, AnotherNextHopStartsTheEntryAgain) {
    RttEstimator rtt;
    rtt.addSample(path(1, 2, 0x0A01), 20000, 0, false);
    uint32_t value = 0;
    EXPECT_FALSE(rtt.estimate(path(1, 2, 0x0A02), value));
    EXPECT_EQ(rtt.size(), 0U);
}

TEST(CmdRtt, ASampleOverAnotherPathIsAFirstSample) {
    RttEstimator rtt;
    rtt.addSample(path(1, 2), 20000, 0, false);
    rtt.addSample(path(1, 2), 28000, 0, false);
    rtt.addSample(path(1, 3), 40000, 0, false);
    EXPECT_EQ(srtt(rtt, path(1, 3)), 40000U);
    EXPECT_EQ(rtt.variationMs(1), 20000U);
    EXPECT_EQ(rtt.size(), 1U);
}

TEST(CmdRtt, AnotherSuperframeScalesTheEstimateAndDoublesTheVariation) {
    RttEstimator rtt;
    rtt.addSample(path(1, 2, 0x0A01, 15000), 20000, 0, false);
    EXPECT_EQ(srtt(rtt, path(1, 2, 0x0A01, 30000)), 40000U);
    EXPECT_EQ(rtt.variationMs(1), 20000U);
    // The entry now belongs to the new superframe: asking again changes nothing.
    EXPECT_EQ(srtt(rtt, path(1, 2, 0x0A01, 30000)), 40000U);
    EXPECT_EQ(rtt.variationMs(1), 20000U);
    // A shorter superframe scales down.
    EXPECT_EQ(srtt(rtt, path(1, 2, 0x0A01, 10000)), 13333U);
    EXPECT_EQ(rtt.variationMs(1), 40000U);
}

TEST(CmdRtt, ASampleWithAnotherSuperframeIsSmoothedIntoTheScaledEstimate) {
    RttEstimator rtt;
    rtt.addSample(path(1, 2, 0x0A01, 15000), 20000, 0, false);
    rtt.addSample(path(1, 2, 0x0A01, 30000), 48000, 0, false);
    // Scaled to SRTT 40000 and RTTVAR 20000, then one smoothing step.
    EXPECT_EQ(rtt.variationMs(1), 17000U);
    EXPECT_EQ(srtt(rtt, path(1, 2, 0x0A01, 30000)), 41000U);
}

TEST(CmdRtt, AnEntryWithoutSuperframeCannotBeScaled) {
    RttEstimator rtt;
    rtt.addSample(path(1, 2, 0x0A01, 0), 20000, 0, false);
    uint32_t value = 0;
    EXPECT_FALSE(rtt.estimate(path(1, 2, 0x0A01, 15000), value));
}

TEST(CmdRtt, KeepsSixteenDestinationsByDefault) {
    RttEstimator rtt;
    for (uint16_t dst = 1; dst <= 16; dst++) rtt.addSample(path(dst), 1000U * dst, 0, false);
    EXPECT_EQ(rtt.size(), 16U);
    for (uint16_t dst = 1; dst <= 16; dst++) EXPECT_EQ(srtt(rtt, path(dst)), 1000U * dst);
}

TEST(CmdRtt, AFullTableReplacesTheEntryUpdatedLongestAgo) {
    RttEstimator rtt(3);
    rtt.addSample(path(1), 1000, 0, false);
    rtt.addSample(path(2), 2000, 0, false);
    rtt.addSample(path(3), 3000, 0, false);
    // Node 1 gets a new sample, so node 2 is now the oldest.
    rtt.addSample(path(1), 1000, 0, false);
    rtt.addSample(path(4), 4000, 0, false);
    EXPECT_EQ(rtt.size(), 3U);
    uint32_t value = 0;
    EXPECT_FALSE(rtt.estimate(path(2), value));
    EXPECT_EQ(srtt(rtt, path(1)), 1000U);
    EXPECT_EQ(srtt(rtt, path(3)), 3000U);
    EXPECT_EQ(srtt(rtt, path(4)), 4000U);
    rtt.addSample(path(5), 5000, 0, false);
    EXPECT_FALSE(rtt.estimate(path(3), value));
}

TEST(CmdRtt, AFreedEntryIsUsedBeforeAnyIsReplaced) {
    RttEstimator rtt(2);
    rtt.addSample(path(1, 2), 1000, 0, false);
    rtt.addSample(path(2, 2), 2000, 0, false);
    uint32_t value = 0;
    EXPECT_FALSE(rtt.estimate(path(1, 5), value));
    rtt.addSample(path(3, 2), 3000, 0, false);
    EXPECT_EQ(srtt(rtt, path(2, 2)), 2000U);
    EXPECT_EQ(srtt(rtt, path(3, 2)), 3000U);
}

TEST(CmdRtt, ZeroEntriesStillKeepsOne) {
    RttEstimator rtt(0);
    rtt.addSample(path(1), 1000, 0, false);
    EXPECT_EQ(srtt(rtt, path(1)), 1000U);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
