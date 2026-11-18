#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "cmdPending.h"

namespace {

constexpr size_t SLOTS = 8;

PendingCommand command(uint16_t dst, uint8_t requestId, bool isSigned,
                       uint32_t timeoutMs = COMMAND_MIN_TIMEOUT_MS) {
    PendingCommand c;
    c.dst = dst;
    c.requestId = requestId;
    c.origin = Origin::MQTT;
    c.originRequestId = static_cast<uint8_t>(requestId + 100);
    c.isSigned = isSigned;
    c.frame = {0x40, '/', 'x', requestId};
    c.timeoutMs = timeoutMs;
    return c;
}

/** The table with the lists of one sweep. */
struct Table {
    PendingCommands pending{SLOTS};
    std::vector<PendingCommand> retransmit;
    std::vector<PendingCommand> expired;

    bool add(const PendingCommand& c, uint32_t nowMs) {
        PendingCommand evicted;
        return pending.add(c, nowMs, evicted);
    }

    void sweep(uint32_t nowMs) {
        retransmit.clear();
        expired.clear();
        pending.sweep(nowMs, retransmit, expired);
    }
};

}  // namespace

TEST(PendingTimeout, FollowsTheSuperframe) {
    EXPECT_EQ(PendingCommands::timeoutMs(0), 30000U);
    EXPECT_EQ(PendingCommands::timeoutMs(10000), 30000U);
    EXPECT_EQ(PendingCommands::timeoutMs(10001), 30003U);
    EXPECT_EQ(PendingCommands::timeoutMs(15200), 45600U);
    EXPECT_EQ(PendingCommands::timeoutMs(89300), 267900U);
    EXPECT_EQ(PendingCommands::timeoutMs(0xFFFFFFFF), 0xFFFFFFFFU);
}

TEST(PendingCommands, AReplyFreesItsSlot) {
    Table t;
    EXPECT_FALSE(t.add(command(0x0B12, 1, true), 1000));
    EXPECT_EQ(t.pending.size(), 1U);
    PendingCommand out;
    ASSERT_TRUE(t.pending.take(0x0B12, 1, out));
    EXPECT_EQ(out.dst, 0x0B12);
    EXPECT_EQ(out.origin, Origin::MQTT);
    EXPECT_EQ(out.originRequestId, 101);
    EXPECT_EQ(out.sentMs, 1000U);
    EXPECT_EQ(t.pending.size(), 0U);
}

TEST(PendingCommands, AReplyMustMatchNodeAndRequestId) {
    Table t;
    t.add(command(0x0B12, 1, true), 0);
    PendingCommand out;
    EXPECT_FALSE(t.pending.take(0x0B13, 1, out));
    EXPECT_FALSE(t.pending.take(0x0B12, 2, out));
    EXPECT_EQ(t.pending.size(), 1U);
}

TEST(PendingCommands, ADuplicateReplyIsNotMatchedTwice) {
    Table t;
    t.add(command(0x0B12, 1, true), 0);
    PendingCommand out;
    EXPECT_TRUE(t.pending.take(0x0B12, 1, out));
    // The node answered the first frame and its retransmission.
    EXPECT_FALSE(t.pending.take(0x0B12, 1, out));
}

TEST(PendingCommands, ExpiresAtTheTimeout) {
    Table t;
    t.add(command(0x0B12, 1, false), 5000);
    t.sweep(5000 + 29999);
    EXPECT_TRUE(t.expired.empty());
    EXPECT_EQ(t.pending.size(), 1U);
    t.sweep(5000 + 30000);
    ASSERT_EQ(t.expired.size(), 1U);
    EXPECT_EQ(t.expired[0].dst, 0x0B12);
    EXPECT_EQ(t.expired[0].requestId, 1);
    EXPECT_EQ(t.expired[0].timeoutMs, 30000U);
    EXPECT_EQ(t.pending.size(), 0U);
    t.sweep(5000 + 60000);
    EXPECT_TRUE(t.expired.empty());
}

TEST(PendingCommands, EachCommandKeepsItsOwnTimeout) {
    Table t;
    t.add(command(0x0001, 1, false, 30000), 0);
    t.add(command(0x0002, 2, false, 267900), 0);
    t.sweep(30000);
    ASSERT_EQ(t.expired.size(), 1U);
    EXPECT_EQ(t.expired[0].dst, 0x0001);
    t.sweep(267899);
    EXPECT_TRUE(t.expired.empty());
    t.sweep(267900);
    ASSERT_EQ(t.expired.size(), 1U);
    EXPECT_EQ(t.expired[0].dst, 0x0002);
}

TEST(PendingCommands, ASignedCommandIsRetransmittedOnceAtHalfTheTimeout) {
    Table t;
    t.add(command(0x0B12, 7, true), 1000);
    t.sweep(1000 + 14999);
    EXPECT_TRUE(t.retransmit.empty());
    t.sweep(1000 + 15000);
    ASSERT_EQ(t.retransmit.size(), 1U);
    EXPECT_EQ(t.retransmit[0].dst, 0x0B12);
    EXPECT_EQ(t.retransmit[0].requestId, 7);
    EXPECT_EQ(t.retransmit[0].frame, (std::vector<uint8_t>{0x40, '/', 'x', 7}));
    EXPECT_TRUE(t.expired.empty());
    for (uint32_t now = 1000 + 16000; now < 1000 + 30000; now += 1000) {
        t.sweep(now);
        EXPECT_TRUE(t.retransmit.empty()) << now;
        EXPECT_TRUE(t.expired.empty()) << now;
    }
    t.sweep(1000 + 30000);
    EXPECT_TRUE(t.retransmit.empty());
    ASSERT_EQ(t.expired.size(), 1U);
    EXPECT_TRUE(t.expired[0].retransmitted);
}

TEST(PendingCommands, AnUnsignedCommandIsNeverRetransmitted) {
    Table t;
    t.add(command(0x0B12, 7, false), 0);
    for (uint32_t now = 0; now < 30000; now += 500) {
        t.sweep(now);
        EXPECT_TRUE(t.retransmit.empty()) << now;
    }
    t.sweep(30000);
    ASSERT_EQ(t.expired.size(), 1U);
    EXPECT_FALSE(t.expired[0].retransmitted);
}

TEST(PendingCommands, ALateSweepExpiresWithoutRetransmitting) {
    Table t;
    t.add(command(0x0B12, 7, true), 0);
    t.sweep(31000);
    EXPECT_TRUE(t.retransmit.empty());
    EXPECT_EQ(t.expired.size(), 1U);
}

TEST(PendingCommands, AReplyAfterTheRetransmissionStillMatches) {
    Table t;
    t.add(command(0x0B12, 7, true), 0);
    t.sweep(15000);
    ASSERT_EQ(t.retransmit.size(), 1U);
    PendingCommand out;
    EXPECT_TRUE(t.pending.take(0x0B12, 7, out));
    EXPECT_TRUE(out.retransmitted);
    t.sweep(30000);
    EXPECT_TRUE(t.expired.empty());
}

TEST(PendingCommands, AReplyAfterTheTimeoutIsNotMatched) {
    Table t;
    t.add(command(0x0B12, 7, true), 0);
    t.sweep(30000);
    ASSERT_EQ(t.expired.size(), 1U);
    PendingCommand out;
    EXPECT_FALSE(t.pending.take(0x0B12, 7, out));
}

TEST(PendingCommands, UsesTheFirstFreeSlotBeforeEvicting) {
    Table t;
    for (uint8_t i = 0; i < SLOTS; i++) EXPECT_FALSE(t.add(command(0x0100 + i, i, false), i));
    EXPECT_EQ(t.pending.size(), SLOTS);
    PendingCommand out;
    ASSERT_TRUE(t.pending.take(0x0103, 3, out));
    // The freed slot is used: nothing is evicted.
    EXPECT_FALSE(t.add(command(0x0200, 50, false), 100));
    EXPECT_EQ(t.pending.size(), SLOTS);
    for (uint8_t i = 0; i < SLOTS; i++) {
        if (i == 3) continue;
        EXPECT_TRUE(t.pending.take(0x0100 + i, i, out)) << int(i);
    }
    EXPECT_TRUE(t.pending.take(0x0200, 50, out));
}

TEST(PendingCommands, AFullTableEvictsTheOldestFirst) {
    Table t;
    // Slot order and age order differ.
    const uint32_t sentMs[SLOTS] = {500, 300, 800, 100, 700, 200, 600, 400};
    for (uint8_t i = 0; i < SLOTS; i++) t.add(command(0x0100 + i, i, false), sentMs[i]);
    const uint8_t expectedOrder[] = {3, 5, 1, 7};
    uint32_t now = 1000;
    for (uint8_t expected : expectedOrder) {
        PendingCommand evicted;
        ASSERT_TRUE(t.pending.add(command(0x0200, static_cast<uint8_t>(100 + expected), false),
                                  now++, evicted));
        EXPECT_EQ(evicted.requestId, expected);
        EXPECT_EQ(evicted.dst, 0x0100 + expected);
        EXPECT_EQ(evicted.originRequestId, expected + 100);
    }
    EXPECT_EQ(t.pending.size(), SLOTS);
    PendingCommand out;
    EXPECT_FALSE(t.pending.take(0x0103, 3, out));
    EXPECT_TRUE(t.pending.take(0x0100, 0, out));
}

TEST(PendingCommands, EvictionOrderSurvivesAClockWrap) {
    Table t;
    uint32_t start = 0xFFFFFFFFU - 3;
    for (uint8_t i = 0; i < SLOTS; i++) t.add(command(0x0100 + i, i, false), start + i);
    PendingCommand evicted;
    ASSERT_TRUE(t.pending.add(command(0x0200, 99, false), start + 20, evicted));
    EXPECT_EQ(evicted.requestId, 0);
    ASSERT_TRUE(t.pending.add(command(0x0200, 98, false), start + 21, evicted));
    EXPECT_EQ(evicted.requestId, 1);
}

TEST(PendingCommands, ExpiryWorksAcrossAClockWrap) {
    Table t;
    t.add(command(0x0B12, 1, true), 0xFFFFFFF0U);
    t.sweep(0xFFFFFFF0U + 14999);
    EXPECT_TRUE(t.retransmit.empty());
    t.sweep(0xFFFFFFF0U + 15000);
    EXPECT_EQ(t.retransmit.size(), 1U);
    t.sweep(0xFFFFFFF0U + 30000);
    EXPECT_EQ(t.expired.size(), 1U);
}

TEST(PendingRequestId, CountsUpAndWraps) {
    PendingCommands pending(SLOTS);
    EXPECT_EQ(pending.nextRequestId(), 1);
    EXPECT_EQ(pending.nextRequestId(), 2);
    for (int i = 0; i < 253; i++) pending.nextRequestId();
    EXPECT_EQ(pending.nextRequestId(), 0);
    EXPECT_EQ(pending.nextRequestId(), 1);
}

TEST(PendingRequestId, SkipsTheIdsOfWaitingCommands) {
    Table t;
    uint8_t first = t.pending.nextRequestId();
    t.add(command(0x0B12, first, true, 600000), 0);
    uint8_t second = t.pending.nextRequestId();
    t.add(command(0x0C40, second, true, 600000), 0);
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 2);
    // 254 more commands are answered at once: the counter comes round to the waiting ids.
    for (int i = 0; i < 254; i++) {
        uint8_t id = t.pending.nextRequestId();
        EXPECT_NE(id, first);
        EXPECT_NE(id, second);
    }
    EXPECT_EQ(t.pending.nextRequestId(), 3);
    // A freed id is used again, and its reply goes to the new command only.
    PendingCommand out;
    ASSERT_TRUE(t.pending.take(0x0B12, first, out));
    for (int i = 0; i < 253; i++) t.pending.nextRequestId();
    EXPECT_EQ(t.pending.nextRequestId(), first);
    t.add(command(0x0D07, first, false), 5000);
    EXPECT_FALSE(t.pending.take(0x0B12, first, out));
    ASSERT_TRUE(t.pending.take(0x0D07, first, out));
    EXPECT_EQ(out.sentMs, 5000U);
}

TEST(PendingCommands, ZeroSlotsStillHoldsOneCommand) {
    PendingCommands pending(0);
    PendingCommand evicted;
    EXPECT_FALSE(pending.add(command(1, 1, false), 0, evicted));
    EXPECT_TRUE(pending.add(command(2, 2, false), 1, evicted));
    EXPECT_EQ(evicted.dst, 1);
    EXPECT_EQ(pending.size(), 1U);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
