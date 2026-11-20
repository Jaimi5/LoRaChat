#include <gtest/gtest.h>

#include <cstdint>

#include "restartGate.h"

namespace {

/** Superframe of the bench with 2 nodes and a sleep fraction of 85 %. */
constexpr uint32_t BENCH_SUPERFRAME_MS = 76950;
constexpr uint32_t POLL_MS = 250;

/** @return the first poll at which @p gate lets the node restart while the queue stays as it is. */
uint32_t restartsAt(RestartGate& gate, size_t queued, uint32_t superframeMs) {
    for (uint32_t elapsedMs = 0; elapsedMs <= 10 * RESTART_CAP_UNKNOWN_MS; elapsedMs += POLL_MS) {
        if (gate.due(elapsedMs, queued, superframeMs)) return elapsedMs;
    }
    return UINT32_MAX;
}

}  // namespace

TEST(RestartGate, TheTimesMatchTheRule) {
    EXPECT_EQ(RESTART_MIN_DELAY_MS, 1000U);
    EXPECT_EQ(RESTART_AIR_TIME_MS, 2000U);
    EXPECT_EQ(RESTART_CAP_MARGIN_MS, 5000U);
    EXPECT_EQ(RESTART_CAP_UNKNOWN_MS, 180000U);
}

TEST(RestartGate, TheCapIsTwoSuperframesAndFiveSeconds) {
    EXPECT_EQ(restartCapMs(BENCH_SUPERFRAME_MS), 158900U);
    EXPECT_EQ(restartCapMs(15200), 35400U);
    EXPECT_EQ(restartCapMs(1), 5002U);
}

TEST(RestartGate, TheCapWithoutASuperframeIsThreeMinutes) {
    EXPECT_EQ(restartCapMs(0), 180000U);
}

TEST(RestartGate, TheCapSaturates) {
    EXPECT_EQ(restartCapMs(UINT32_MAX), UINT32_MAX);
    EXPECT_EQ(restartCapMs(UINT32_MAX / 2), UINT32_MAX);
}

TEST(RestartGate, AnEmptyQueueRestartsThreeSecondsAfterTheRequest) {
    RestartGate gate;
    EXPECT_FALSE(gate.due(0, 0, BENCH_SUPERFRAME_MS));
    EXPECT_FALSE(gate.due(999, 0, BENCH_SUPERFRAME_MS));
    EXPECT_FALSE(gate.due(1000, 0, BENCH_SUPERFRAME_MS));
    EXPECT_FALSE(gate.due(2999, 0, BENCH_SUPERFRAME_MS));
    EXPECT_TRUE(gate.due(3000, 0, BENCH_SUPERFRAME_MS));

    RestartGate unknown;
    EXPECT_EQ(restartsAt(unknown, 0, 0), 3000U);
}

TEST(RestartGate, NeverBeforeOneSecondWhateverTheQueue) {
    for (size_t queued : {size_t{0}, size_t{1}, size_t{5}}) {
        RestartGate gate;
        for (uint32_t elapsedMs = 0; elapsedMs < RESTART_MIN_DELAY_MS; elapsedMs += 50) {
            EXPECT_FALSE(gate.due(elapsedMs, queued, 0)) << queued << " at " << elapsedMs;
        }
    }
}

TEST(RestartGate, WaitsForTheQueueToEmptyAndTwoSecondsMore) {
    RestartGate gate;
    // The reply waits for the TX data slot of the node, 60 s away.
    for (uint32_t elapsedMs = 0; elapsedMs < 60000; elapsedMs += POLL_MS) {
        ASSERT_FALSE(gate.due(elapsedMs, 1, BENCH_SUPERFRAME_MS)) << elapsedMs;
    }
    EXPECT_FALSE(gate.due(60000, 0, BENCH_SUPERFRAME_MS));
    EXPECT_FALSE(gate.due(61750, 0, BENCH_SUPERFRAME_MS));
    EXPECT_TRUE(gate.due(62000, 0, BENCH_SUPERFRAME_MS));
    EXPECT_TRUE(gate.due(62250, 0, BENCH_SUPERFRAME_MS));
}

TEST(RestartGate, AQueueSeenEmptyBeforeOneSecondDoesNotCount) {
    RestartGate gate;
    // The reply joins the queue after the command handler has returned.
    EXPECT_FALSE(gate.due(250, 0, BENCH_SUPERFRAME_MS));
    EXPECT_FALSE(gate.due(1000, 1, BENCH_SUPERFRAME_MS));
    EXPECT_FALSE(gate.due(3000, 1, BENCH_SUPERFRAME_MS));
    EXPECT_FALSE(gate.due(4000, 0, BENCH_SUPERFRAME_MS));
    EXPECT_FALSE(gate.due(5999, 0, BENCH_SUPERFRAME_MS));
    EXPECT_TRUE(gate.due(6000, 0, BENCH_SUPERFRAME_MS));
}

TEST(RestartGate, AMessageQueuedAfterTheReplyLeftDoesNotDelayTheRestart) {
    RestartGate gate;
    EXPECT_FALSE(gate.due(1000, 1, BENCH_SUPERFRAME_MS));
    EXPECT_FALSE(gate.due(10000, 0, BENCH_SUPERFRAME_MS));
    EXPECT_FALSE(gate.due(11000, 1, BENCH_SUPERFRAME_MS));
    EXPECT_TRUE(gate.due(12000, 1, BENCH_SUPERFRAME_MS));
}

TEST(RestartGate, AQueueThatNeverEmptiesRestartsAtTheCap) {
    RestartGate known;
    EXPECT_FALSE(known.due(158899, 3, BENCH_SUPERFRAME_MS));
    EXPECT_TRUE(known.due(158900, 3, BENCH_SUPERFRAME_MS));

    RestartGate unknown;
    EXPECT_FALSE(unknown.due(179999, 3, 0));
    EXPECT_TRUE(unknown.due(180000, 3, 0));

    RestartGate polled;
    EXPECT_EQ(restartsAt(polled, 1, 15200), 35500U);
}

TEST(RestartGate, TheCapFollowsTheSuperframeOfEachLook) {
    RestartGate gate;
    EXPECT_FALSE(gate.due(40000, 1, 0));
    // The mesh has learnt its superframe since: 2 * 15.2 s + 5 s have already passed.
    EXPECT_TRUE(gate.due(40250, 1, 15200));
}

TEST(RestartGate, TheCapCutsTheWaitForTheAirTime) {
    RestartGate gate;
    EXPECT_FALSE(gate.due(34000, 1, 15200));
    EXPECT_FALSE(gate.due(35000, 0, 15200));
    EXPECT_TRUE(gate.due(35400, 0, 15200));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
