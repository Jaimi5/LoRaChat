#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "cmdPending.h"
#include "cmdStatus.h"

namespace {

constexpr size_t SLOTS = 8;
constexpr uint32_t SUPERFRAMES_MS[] = {15000, 30000, 90000};

AttemptTiming timing(uint32_t slotWaitMs, uint8_t hops, uint32_t superframeMs,
                     uint32_t allowanceMs = 0) {
    AttemptTiming t;
    t.slotWaitMs = slotWaitMs;
    t.hops = hops;
    t.superframeMs = superframeMs;
    t.allowanceMs = allowanceMs;
    return t;
}

/** One hop, a 10 s superframe and a slot 3 s away: A = 3 + 20 + 2 = 25 s. */
const AttemptTiming SHORT = timing(3000, 1, 10000);
constexpr uint32_t SHORT_A = 25000;
/** The give-up time of a signed command before its second transmission: A + (10 + 20 + 2) s. */
constexpr uint32_t SHORT_SIGNED_GIVE_UP = 57000;

PendingCommand command(uint16_t dst, uint8_t requestId, bool isSigned,
                       const AttemptTiming& t = SHORT) {
    PendingCommand c;
    c.dst = dst;
    c.requestId = requestId;
    c.origin = Origin::MQTT;
    c.originRequestId = static_cast<uint8_t>(requestId + 100);
    c.isSigned = isSigned;
    c.name = "/x";
    c.frame = {0x40, '/', 'x', requestId};
    c.timing = t;
    return c;
}

/** The table with the lists of one sweep. */
struct Table {
    PendingCommands pending{SLOTS};
    std::vector<PendingCommand> retransmit;
    std::vector<PendingCommand> expired;

    bool add(PendingCommand c, uint32_t nowMs) {
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

TEST(AttemptTime, FollowsTheFormulaForOneToThreeHops) {
    // A = w + (h + 1) * S + ceil(P / S) * S + g, with g = 2 s.
    EXPECT_EQ(attemptTimeMs(timing(4000, 1, 15000)), 4000U + 30000U + 2000U);
    EXPECT_EQ(attemptTimeMs(timing(4000, 2, 15000)), 4000U + 45000U + 2000U);
    EXPECT_EQ(attemptTimeMs(timing(4000, 3, 15000)), 4000U + 60000U + 2000U);
    EXPECT_EQ(attemptTimeMs(timing(11000, 1, 30000)), 11000U + 60000U + 2000U);
    EXPECT_EQ(attemptTimeMs(timing(11000, 2, 30000)), 11000U + 90000U + 2000U);
    EXPECT_EQ(attemptTimeMs(timing(11000, 3, 30000)), 11000U + 120000U + 2000U);
    EXPECT_EQ(attemptTimeMs(timing(89000, 1, 90000)), 89000U + 180000U + 2000U);
    EXPECT_EQ(attemptTimeMs(timing(89000, 2, 90000)), 89000U + 270000U + 2000U);
    EXPECT_EQ(attemptTimeMs(timing(89000, 3, 90000)), 89000U + 360000U + 2000U);
}

TEST(AttemptTime, TheAllowanceCountsInWholeSuperframes) {
    EXPECT_EQ(attemptTimeMs(timing(0, 1, 15000, 0)), 32000U);
    EXPECT_EQ(attemptTimeMs(timing(0, 1, 15000, 1)), 47000U);
    EXPECT_EQ(attemptTimeMs(timing(0, 1, 15000, 10)), 47000U);
    EXPECT_EQ(attemptTimeMs(timing(0, 1, 15000, 15000)), 47000U);
    EXPECT_EQ(attemptTimeMs(timing(0, 1, 15000, 15001)), 62000U);
}

TEST(AttemptTime, WithoutASuperframeOnlyTheGuardAndTheAllowanceRemain) {
    EXPECT_EQ(attemptTimeMs(timing(0, 2, 0)), COMMAND_GUARD_MS);
    EXPECT_EQ(attemptTimeMs(timing(0, 2, 0, 300)), COMMAND_GUARD_MS + 300U);
}

TEST(AttemptTime, Saturates) {
    EXPECT_EQ(attemptTimeMs(timing(0xFFFFFFFF, 255, 0xFFFFFFFF, 0xFFFFFFFF)), 0xFFFFFFFFU);
}

TEST(ExpectedReply, UsesTheModelWithoutSamplesAndTheEstimateWithThem) {
    // Model: w + h * S. Measured: w + SRTT + g.
    EXPECT_EQ(expectedReplyMs(timing(4000, 2, 15000), false, 0), 34000U);
    EXPECT_EQ(expectedReplyMs(timing(4000, 2, 15000), false, 99999), 34000U);
    EXPECT_EQ(expectedReplyMs(timing(4000, 2, 15000), true, 21000), 27000U);
    EXPECT_EQ(expectedReplyMs(timing(0xFFFFFFFF, 2, 15000), true, 21000), 0xFFFFFFFFU);
}

/** The slots of the bench gateway: 81 slots of 950 ms, TX data in slots 2 and 3. */
constexpr uint32_t BENCH_SLOT_MS = 950;
constexpr uint32_t BENCH_SUPERFRAME_MS = 81 * BENCH_SLOT_MS;
/** Time before a slot start within which the mesh already counts the slot after it. */
constexpr uint32_t TOLERANCE_MS = 200;

TxSlots txSlots(uint32_t superframeMs, std::vector<uint32_t> startsMs) {
    TxSlots slots;
    slots.superframeMs = superframeMs;
    slots.startsMs = std::move(startsMs);
    return slots;
}

const TxSlots BENCH_SLOTS = txSlots(BENCH_SUPERFRAME_MS, {2 * BENCH_SLOT_MS, 3 * BENCH_SLOT_MS});

TEST(QueuedSlotWait, AnEmptyQueueWaitsForTheNextSlotOnly) {
    EXPECT_EQ(queuedSlotWaitMs(4300, 0, txSlots(15200, {1900}), TOLERANCE_MS), 4300U);
    EXPECT_EQ(queuedSlotWaitMs(57500, 0, BENCH_SLOTS, TOLERANCE_MS), 57500U);
}

TEST(QueuedSlotWait, WithOneSlotEachQueuedMessageCostsASuperframe) {
    TxSlots one = txSlots(15200, {1900});
    EXPECT_EQ(queuedSlotWaitMs(4300, 1, one, TOLERANCE_MS), 4300U + 15200U);
    EXPECT_EQ(queuedSlotWaitMs(4300, 3, one, TOLERANCE_MS), 4300U + 3 * 15200U);
    // Right before the slot the mesh already answers with its next turn.
    EXPECT_EQ(queuedSlotWaitMs(15300, 2, one, TOLERANCE_MS), 15300U + 2 * 15200U);
}

TEST(QueuedSlotWait, WithTwoSlotsTwoMessagesLeaveInOneSuperframe) {
    // The next slot is the first of the pair: the queued message takes it, this one the second.
    EXPECT_EQ(queuedSlotWaitMs(57500, 1, BENCH_SLOTS, TOLERANCE_MS), 57500U + BENCH_SLOT_MS);
    EXPECT_EQ(queuedSlotWaitMs(57500, 2, BENCH_SLOTS, TOLERANCE_MS), 57500U + BENCH_SUPERFRAME_MS);
    EXPECT_EQ(queuedSlotWaitMs(57500, 3, BENCH_SLOTS, TOLERANCE_MS),
              57500U + BENCH_SUPERFRAME_MS + BENCH_SLOT_MS);
    EXPECT_EQ(queuedSlotWaitMs(57500, 5, BENCH_SLOTS, TOLERANCE_MS),
              57500U + 2 * BENCH_SUPERFRAME_MS + BENCH_SLOT_MS);
}

TEST(QueuedSlotWait, ANextSlotThatMayBeTheLastOfTheSuperframeCostsTheLongGap) {
    // Less than a slot away, the next slot can be the second of the pair. The queued message
    // then takes it and this one waits for the first slot of the next superframe.
    const uint32_t longGap = BENCH_SUPERFRAME_MS - BENCH_SLOT_MS;
    EXPECT_EQ(queuedSlotWaitMs(600, 1, BENCH_SLOTS, TOLERANCE_MS), 600U + longGap);
    EXPECT_EQ(queuedSlotWaitMs(BENCH_SLOT_MS + TOLERANCE_MS, 1, BENCH_SLOTS, TOLERANCE_MS),
              BENCH_SLOT_MS + TOLERANCE_MS + longGap);
    EXPECT_EQ(queuedSlotWaitMs(BENCH_SLOT_MS + TOLERANCE_MS + 1, 1, BENCH_SLOTS, TOLERANCE_MS),
              2 * BENCH_SLOT_MS + TOLERANCE_MS + 1);
    EXPECT_EQ(queuedSlotWaitMs(600, 3, BENCH_SLOTS, TOLERANCE_MS),
              600U + BENCH_SUPERFRAME_MS + longGap);
}

TEST(QueuedSlotWait, SlotsSpreadOverTheSuperframeCostTheirLongestRun) {
    TxSlots spread = txSlots(60000, {30000, 0});
    EXPECT_EQ(queuedSlotWaitMs(10000, 1, spread, TOLERANCE_MS), 10000U + 30000U);
    TxSlots three = txSlots(60000, {1000, 2000, 40000});
    // 10 s away the next slot is the one at 40 s or the one at 1 s.
    EXPECT_EQ(queuedSlotWaitMs(10000, 1, three, TOLERANCE_MS), 10000U + 21000U);
    EXPECT_EQ(queuedSlotWaitMs(10000, 2, three, TOLERANCE_MS), 10000U + 39000U);
    // 30 s away it can only be the one at 40 s.
    EXPECT_EQ(queuedSlotWaitMs(30000, 1, three, TOLERANCE_MS), 30000U + 21000U);
    EXPECT_EQ(queuedSlotWaitMs(30000, 2, three, TOLERANCE_MS), 30000U + 22000U);
    EXPECT_EQ(queuedSlotWaitMs(30000, 4, three, TOLERANCE_MS), 30000U + 60000U + 21000U);
}

TEST(QueuedSlotWait, UnknownSlotsCostASuperframePerQueuedMessage) {
    EXPECT_EQ(queuedSlotWaitMs(4300, 2, txSlots(15200, {}), TOLERANCE_MS), 4300U + 2 * 15200U);
    EXPECT_EQ(queuedSlotWaitMs(4300, 2, txSlots(0, {1900}), TOLERANCE_MS), 4300U);
    // A wait that fits no slot of the table is not trusted to say which slot comes next.
    EXPECT_EQ(queuedSlotWaitMs(90000, 1, BENCH_SLOTS, TOLERANCE_MS),
              90000U + BENCH_SUPERFRAME_MS - BENCH_SLOT_MS);
    // Two slots at the same place count as a whole superframe apart.
    EXPECT_EQ(queuedSlotWaitMs(4300, 1, txSlots(15200, {1900, 1900}), TOLERANCE_MS),
              4300U + 15200U);
}

TEST(QueuedSlotWait, StaysBetweenTheSlotShareAndASuperframePerMessage) {
    const std::vector<TxSlots> tables = {txSlots(15200, {1900}), BENCH_SLOTS,
                                         txSlots(60000, {0, 30000}),
                                         txSlots(60000, {1000, 2000, 40000})};
    for (const TxSlots& slots : tables) {
        const size_t count = slots.startsMs.size();
        for (uint32_t untilMs : {1U, 500U, 1100U, 5000U, 29000U, 59000U}) {
            for (size_t queued = 0; queued <= 7; queued++) {
                uint32_t wait = queuedSlotWaitMs(untilMs, queued, slots, TOLERANCE_MS);
                EXPECT_GE(wait, untilMs + (queued / count) * slots.superframeMs);
                EXPECT_LE(wait, untilMs + queued * slots.superframeMs);
                if (queued > 0) {
                    EXPECT_GE(wait, queuedSlotWaitMs(untilMs, queued - 1, slots, TOLERANCE_MS));
                }
            }
        }
    }
}

TEST(QueuedSlotWait, Saturates) {
    EXPECT_EQ(queuedSlotWaitMs(UINT32_MAX - 5, 3, BENCH_SLOTS, TOLERANCE_MS), UINT32_MAX);
    EXPECT_EQ(queuedSlotWaitMs(1000, 100000, txSlots(90000, {0}), TOLERANCE_MS), UINT32_MAX);
}

TEST(CeilSeconds, RoundsUp) {
    EXPECT_EQ(ceilSeconds(0), 0U);
    EXPECT_EQ(ceilSeconds(1), 1U);
    EXPECT_EQ(ceilSeconds(1000), 1U);
    EXPECT_EQ(ceilSeconds(1001), 2U);
    EXPECT_EQ(ceilSeconds(0xFFFFFFFF), 4294968U);
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
    EXPECT_EQ(out.name, "/x");
    EXPECT_EQ(out.attempts(), 1U);
    EXPECT_EQ(t.pending.size(), 0U);
}

TEST(PendingCommands, AddFillsTheTimesOfTheCommand) {
    PendingCommands pending(SLOTS);
    PendingCommand evicted;
    PendingCommand plain = command(1, 1, false);
    pending.add(plain, 500, evicted);
    EXPECT_EQ(plain.sentMs, 500U);
    EXPECT_EQ(plain.attemptMs, SHORT_A);
    EXPECT_EQ(plain.giveUpMs, SHORT_A);
    PendingCommand signedCommand = command(2, 2, true);
    pending.add(signedCommand, 500, evicted);
    EXPECT_EQ(signedCommand.attemptMs, SHORT_A);
    EXPECT_EQ(signedCommand.giveUpMs, SHORT_SIGNED_GIVE_UP);
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
    EXPECT_FALSE(t.pending.takeLate(0x0B12, 1, 100, out));
}

TEST(PendingCommands, AnUnsignedCommandIsGivenUpAtAAndNeverSentAgain) {
    for (uint32_t superframeMs : SUPERFRAMES_MS) {
        for (uint8_t hops = 1; hops <= 3; hops++) {
            SCOPED_TRACE(testing::Message() << "S=" << superframeMs << " h=" << int(hops));
            const uint32_t slotWaitMs = superframeMs / 3;
            const uint32_t a = slotWaitMs + (hops + 1U) * superframeMs + 2000;
            Table t;
            t.add(command(0x0B12, 7, false, timing(slotWaitMs, hops, superframeMs)), 5000);
            for (uint32_t waited = 0; waited < a; waited += 500) {
                t.sweep(5000 + waited);
                ASSERT_TRUE(t.retransmit.empty()) << waited;
                ASSERT_TRUE(t.expired.empty()) << waited;
            }
            t.sweep(5000 + a - 1);
            EXPECT_TRUE(t.expired.empty());
            t.sweep(5000 + a);
            EXPECT_TRUE(t.retransmit.empty());
            ASSERT_EQ(t.expired.size(), 1U);
            EXPECT_EQ(t.expired[0].attempts(), 1U);
            EXPECT_EQ(t.expired[0].waitedMs(5000 + a), a);
            EXPECT_EQ(t.pending.size(), 0U);
        }
    }
}

TEST(PendingCommands, ASignedCommandIsSentAgainOnceAtA1AndGivenUpAtA1PlusA2) {
    for (uint32_t superframeMs : SUPERFRAMES_MS) {
        for (uint8_t hops = 1; hops <= 3; hops++) {
            SCOPED_TRACE(testing::Message() << "S=" << superframeMs << " h=" << int(hops));
            const uint32_t firstWaitMs = superframeMs / 3;
            const uint32_t secondWaitMs = superframeMs / 5;
            const uint32_t a1 = firstWaitMs + (hops + 1U) * superframeMs + 2000;
            const uint32_t a2 = secondWaitMs + (hops + 1U) * superframeMs + 2000;
            const uint32_t start = 7000;
            Table t;
            t.add(command(0x0B12, 7, true, timing(firstWaitMs, hops, superframeMs)), start);

            // Nothing is sent again while a lossless reply may still be on its way.
            for (uint32_t waited = 0; waited < a1; waited += 500) {
                t.sweep(start + waited);
                ASSERT_TRUE(t.retransmit.empty()) << waited;
                ASSERT_TRUE(t.expired.empty()) << waited;
            }
            t.sweep(start + a1 - 1);
            EXPECT_TRUE(t.retransmit.empty());
            t.sweep(start + a1);
            ASSERT_EQ(t.retransmit.size(), 1U);
            EXPECT_EQ(t.retransmit[0].dst, 0x0B12);
            EXPECT_EQ(t.retransmit[0].requestId, 7);
            EXPECT_EQ(t.retransmit[0].frame, (std::vector<uint8_t>{0x40, '/', 'x', 7}));
            EXPECT_TRUE(t.expired.empty());
            PendingCommand second;
            ASSERT_TRUE(t.pending.secondAttempt(0x0B12, 7, start + a1,
                                                timing(secondWaitMs, hops, superframeMs),
                                                secondWaitMs + 1000, second));
            EXPECT_EQ(second.giveUpMs, a1 + a2);
            EXPECT_EQ(second.remainingMs(start + a1), a2);
            EXPECT_EQ(second.expectedInMs(start + a1), secondWaitMs + 1000);
            EXPECT_EQ(second.attempts(), 2U);

            // Exactly one retransmission, and the command waits A2 more.
            for (uint32_t waited = a1 + 500; waited < a1 + a2; waited += 500) {
                t.sweep(start + waited);
                ASSERT_TRUE(t.retransmit.empty()) << waited;
                ASSERT_TRUE(t.expired.empty()) << waited;
            }
            t.sweep(start + a1 + a2 - 1);
            EXPECT_TRUE(t.expired.empty());
            t.sweep(start + a1 + a2);
            EXPECT_TRUE(t.retransmit.empty());
            ASSERT_EQ(t.expired.size(), 1U);
            EXPECT_EQ(t.expired[0].attempts(), 2U);
            EXPECT_EQ(t.pending.size(), 0U);
        }
    }
}

TEST(PendingCommands, TheAllowanceLengthensBothAttempts) {
    Table t;
    // One hop, S = 15 s, a slot 1 s away, 10 ms to run: A1 = 1 + 30 + 15 + 2 = 48 s.
    t.add(command(0x0B12, 7, true, timing(1000, 1, 15000, 10)), 0);
    t.sweep(47999);
    EXPECT_TRUE(t.retransmit.empty());
    t.sweep(48000);
    ASSERT_EQ(t.retransmit.size(), 1U);
    // Until the second transmission is recorded: A1 + (15 + 30 + 15 + 2) s.
    EXPECT_EQ(t.retransmit[0].giveUpMs, 48000U + 62000U);
}

TEST(PendingCommands, ALateSweepGivesUpWithoutSendingAgain) {
    Table t;
    t.add(command(0x0B12, 7, true), 0);
    t.sweep(SHORT_SIGNED_GIVE_UP + 1000);
    EXPECT_TRUE(t.retransmit.empty());
    ASSERT_EQ(t.expired.size(), 1U);
    EXPECT_EQ(t.expired[0].attempts(), 1U);
}

TEST(PendingCommands, AReplyAfterTheRetransmissionStillMatches) {
    Table t;
    t.add(command(0x0B12, 7, true), 0);
    t.sweep(SHORT_A);
    ASSERT_EQ(t.retransmit.size(), 1U);
    PendingCommand out;
    EXPECT_TRUE(t.pending.take(0x0B12, 7, out));
    EXPECT_TRUE(out.retransmitted);
    EXPECT_EQ(out.attempts(), 2U);
    t.sweep(SHORT_SIGNED_GIVE_UP);
    EXPECT_TRUE(t.expired.empty());
}

TEST(PendingCommands, TheSecondAttemptOfACommandThatLeftIsRefused) {
    Table t;
    t.add(command(0x0B12, 7, true), 0);
    PendingCommand out;
    ASSERT_TRUE(t.pending.take(0x0B12, 7, out));
    EXPECT_FALSE(t.pending.secondAttempt(0x0B12, 7, SHORT_A, SHORT, 0, out));
    EXPECT_FALSE(t.pending.giveUp(0x0B12, 7, SHORT_A, out));
}

TEST(PendingCommands, EachCommandKeepsItsOwnTimes) {
    Table t;
    t.add(command(0x0001, 1, false, timing(0, 1, 14000)), 0);
    t.add(command(0x0002, 2, false, timing(0, 3, 90000)), 0);
    t.sweep(30000);
    ASSERT_EQ(t.expired.size(), 1U);
    EXPECT_EQ(t.expired[0].dst, 0x0001);
    t.sweep(361999);
    EXPECT_TRUE(t.expired.empty());
    t.sweep(362000);
    ASSERT_EQ(t.expired.size(), 1U);
    EXPECT_EQ(t.expired[0].dst, 0x0002);
}

TEST(PendingRepeat, TheSameRequestAgainIsRecognisedWhileItWaits) {
    Table t;
    PendingCommand first = command(0x0B12, 7, true);
    t.add(first, 1000);
    const PendingCommand* waiting =
        t.pending.findRepeat(Origin::MQTT, 0x0B12, first.originRequestId, first.frame);
    ASSERT_NE(waiting, nullptr);
    EXPECT_EQ(waiting->requestId, 7);
    EXPECT_EQ(waiting->sentMs, 1000U);
    EXPECT_EQ(waiting->remainingMs(11000), SHORT_SIGNED_GIVE_UP - 10000U);
    // It stays recognised after the retransmission.
    t.sweep(1000 + SHORT_A);
    ASSERT_EQ(t.retransmit.size(), 1U);
    EXPECT_NE(t.pending.findRepeat(Origin::MQTT, 0x0B12, first.originRequestId, first.frame),
              nullptr);
}

TEST(PendingRepeat, AnotherDestinationRequestIdFrameOrChannelIsANewRequest) {
    Table t;
    PendingCommand first = command(0x0B12, 7, true);
    t.add(first, 0);
    std::vector<uint8_t> otherFrame = first.frame;
    otherFrame.back() ^= 0x01;
    uint8_t id = first.originRequestId;
    EXPECT_EQ(t.pending.findRepeat(Origin::MQTT, 0x0B13, id, first.frame), nullptr);
    EXPECT_EQ(t.pending.findRepeat(Origin::MQTT, 0x0B12, static_cast<uint8_t>(id + 1), first.frame),
              nullptr);
    EXPECT_EQ(t.pending.findRepeat(Origin::MQTT, 0x0B12, id, otherFrame), nullptr);
    EXPECT_EQ(t.pending.findRepeat(Origin::SERIAL_CONSOLE, 0x0B12, id, first.frame), nullptr);
}

TEST(PendingRepeat, ARequestThatWasAnsweredOrGivenUpIsNewAgain) {
    Table t;
    PendingCommand first = command(0x0B12, 7, false);
    t.add(first, 0);
    PendingCommand out;
    ASSERT_TRUE(t.pending.take(0x0B12, 7, out));
    EXPECT_EQ(t.pending.findRepeat(Origin::MQTT, 0x0B12, first.originRequestId, first.frame),
              nullptr);
    t.add(first, 0);
    t.sweep(SHORT_A);
    ASSERT_EQ(t.expired.size(), 1U);
    EXPECT_EQ(t.pending.findRepeat(Origin::MQTT, 0x0B12, first.originRequestId, first.frame),
              nullptr);
}

TEST(PendingLate, ALateReplyIsMatchedForTenMinutesAfterTheTimeout) {
    Table t;
    t.add(command(0x0B12, 7, false), 1000);
    t.sweep(1000 + SHORT_A);
    ASSERT_EQ(t.expired.size(), 1U);
    uint32_t expiredAt = 1000 + SHORT_A;
    EXPECT_EQ(t.pending.lateSize(expiredAt), 1U);
    PendingCommand out;
    // The waiting table no longer has it.
    EXPECT_FALSE(t.pending.take(0x0B12, 7, out));
    EXPECT_FALSE(t.pending.takeLate(0x0B13, 7, expiredAt + 1, out));
    EXPECT_FALSE(t.pending.takeLate(0x0B12, 8, expiredAt + 1, out));
    ASSERT_TRUE(t.pending.takeLate(0x0B12, 7, expiredAt + COMMAND_LATE_REPLY_MS, out));
    EXPECT_EQ(out.dst, 0x0B12);
    EXPECT_EQ(out.originRequestId, 107);
    EXPECT_EQ(out.origin, Origin::MQTT);
    EXPECT_EQ(out.name, "/x");
    EXPECT_EQ(out.sentMs, 1000U);
    EXPECT_EQ(out.timing.slotWaitMs, 3000U);
    EXPECT_EQ(out.attempts(), 1U);
    // The frame is not kept.
    EXPECT_TRUE(out.frame.empty());
    // A second late reply finds nothing.
    EXPECT_FALSE(t.pending.takeLate(0x0B12, 7, expiredAt + COMMAND_LATE_REPLY_MS, out));
}

TEST(PendingLate, ALateReplyAfterTenMinutesIsNotMatched) {
    Table t;
    t.add(command(0x0B12, 7, false), 1000);
    t.sweep(1000 + SHORT_A);
    uint32_t expiredAt = 1000 + SHORT_A;
    EXPECT_EQ(COMMAND_LATE_REPLY_MS, 600000U);
    PendingCommand out;
    EXPECT_FALSE(t.pending.takeLate(0x0B12, 7, expiredAt + COMMAND_LATE_REPLY_MS + 1, out));
    EXPECT_EQ(t.pending.lateSize(expiredAt + COMMAND_LATE_REPLY_MS + 1), 0U);
    // It is forgotten for good.
    EXPECT_FALSE(t.pending.takeLate(0x0B12, 7, expiredAt + 1, out));
}

TEST(PendingLate, ACommandGivenUpByTheCallerIsRememberedToo) {
    Table t;
    t.add(command(0x0B12, 7, true), 0);
    PendingCommand out;
    ASSERT_TRUE(t.pending.giveUp(0x0B12, 7, 26000, out));
    EXPECT_EQ(out.dst, 0x0B12);
    EXPECT_EQ(t.pending.size(), 0U);
    ASSERT_TRUE(t.pending.takeLate(0x0B12, 7, 30000, out));
    EXPECT_EQ(out.originRequestId, 107);
}

TEST(PendingCancel, AWaitingCommandIsGivenUpAndNeverSentAgain) {
    Table t;
    PendingCommand first = command(0x0B12, 7, true);
    t.add(first, 1000);
    t.add(command(0x0B13, 8, true), 1000);
    PendingCommand out;
    ASSERT_TRUE(t.pending.cancel(Origin::MQTT, 0x0B12, first.originRequestId, 5000, out));
    EXPECT_EQ(out.dst, 0x0B12);
    EXPECT_EQ(out.requestId, 7);
    EXPECT_EQ(out.originRequestId, 107);
    EXPECT_EQ(out.origin, Origin::MQTT);
    EXPECT_EQ(t.pending.size(), 1U);
    EXPECT_EQ(formatReplyPayload(out.originRequestId, CommandResult(CommandStatus::CANCELLED)),
              "107 E12");

    // Neither the retransmission nor the timeout of the cancelled command follows.
    t.sweep(1000 + SHORT_A);
    ASSERT_EQ(t.retransmit.size(), 1U);
    EXPECT_EQ(t.retransmit[0].dst, 0x0B13);
    t.sweep(1000 + SHORT_SIGNED_GIVE_UP);
    ASSERT_EQ(t.expired.size(), 1U);
    EXPECT_EQ(t.expired[0].dst, 0x0B13);
    // The same request is new again, and a second cancel finds nothing.
    EXPECT_EQ(t.pending.findRepeat(Origin::MQTT, 0x0B12, first.originRequestId, first.frame),
              nullptr);
    EXPECT_FALSE(t.pending.cancel(Origin::MQTT, 0x0B12, first.originRequestId, 6000, out));
}

TEST(PendingCancel, ACancelledCommandIsRememberedForItsLateReplyForTenMinutes) {
    Table t;
    t.add(command(0x0B12, 7, true), 1000);
    PendingCommand out;
    ASSERT_TRUE(t.pending.cancel(Origin::MQTT, 0x0B12, 107, 5000, out));
    EXPECT_EQ(t.pending.lateSize(5000), 1U);
    EXPECT_FALSE(t.pending.take(0x0B12, 7, out));
    PendingCommand late;
    ASSERT_TRUE(t.pending.takeLate(0x0B12, 7, 5000 + COMMAND_LATE_REPLY_MS, late));
    EXPECT_EQ(late.originRequestId, 107);
    EXPECT_EQ(late.sentMs, 1000U);
    EXPECT_EQ(late.attempts(), 1U);
    EXPECT_TRUE(late.frame.empty());

    t.add(command(0x0B12, 9, false), 1000);
    ASSERT_TRUE(t.pending.cancel(Origin::MQTT, 0x0B12, 109, 5000, out));
    EXPECT_FALSE(t.pending.takeLate(0x0B12, 9, 5000 + COMMAND_LATE_REPLY_MS + 1, late));
}

TEST(PendingCancel, ACancelAfterTheRetransmissionKeepsTheAttempts) {
    Table t;
    t.add(command(0x0B12, 7, true), 0);
    t.sweep(SHORT_A);
    ASSERT_EQ(t.retransmit.size(), 1U);
    PendingCommand out;
    ASSERT_TRUE(t.pending.secondAttempt(0x0B12, 7, SHORT_A, SHORT, 0, out));
    ASSERT_TRUE(t.pending.cancel(Origin::MQTT, 0x0B12, 107, SHORT_A + 1000, out));
    EXPECT_EQ(out.attempts(), 2U);
    t.sweep(SHORT_A + SHORT_A);
    EXPECT_TRUE(t.expired.empty());
}

TEST(PendingCancel, NothingHappensWithoutAWaitingCommandOfThatRequest) {
    Table t;
    PendingCommand serial = command(0x0B12, 7, true);
    serial.origin = Origin::SERIAL_CONSOLE;
    t.add(serial, 0);
    PendingCommand out;
    out.dst = 0x0042;
    // Never seen: another destination, another request id, another channel.
    EXPECT_FALSE(t.pending.cancel(Origin::MQTT, 0x0B13, 107, 1000, out));
    EXPECT_FALSE(t.pending.cancel(Origin::MQTT, 0x0B12, 108, 1000, out));
    EXPECT_FALSE(t.pending.cancel(Origin::MQTT, 0x0B12, 107, 1000, out));
    // The LoRa request id is not the one the cancel names.
    EXPECT_FALSE(t.pending.cancel(Origin::SERIAL_CONSOLE, 0x0B12, 7, 1000, out));
    EXPECT_EQ(out.dst, 0x0042);
    EXPECT_EQ(t.pending.size(), 1U);
    EXPECT_EQ(t.pending.lateSize(1000), 0U);

    // Already answered.
    t.add(command(0x0B14, 8, false), 0);
    ASSERT_TRUE(t.pending.take(0x0B14, 8, out));
    EXPECT_FALSE(t.pending.cancel(Origin::MQTT, 0x0B14, 108, 1000, out));
    // Already timed out: the remembered command stays for its late reply.
    t.add(command(0x0B15, 9, false), 0);
    t.sweep(SHORT_A);
    ASSERT_EQ(t.expired.size(), 1U);
    EXPECT_FALSE(t.pending.cancel(Origin::MQTT, 0x0B15, 109, SHORT_A + 1, out));
    EXPECT_EQ(t.pending.lateSize(SHORT_A + 1), 1U);
}

TEST(PendingCancel, EveryWaitingCommandOfTheRequestIsGivenUp) {
    Table t;
    PendingCommand first = command(0x0B12, 7, true);
    PendingCommand second = command(0x0B12, 8, true);
    second.originRequestId = first.originRequestId;
    t.add(first, 0);
    t.add(second, 500);
    PendingCommand out;
    ASSERT_TRUE(t.pending.cancel(Origin::MQTT, 0x0B12, first.originRequestId, 1000, out));
    EXPECT_EQ(out.requestId, 7);
    EXPECT_EQ(t.pending.size(), 0U);
    EXPECT_EQ(t.pending.lateSize(1000), 2U);
}

TEST(PendingLate, AnEvictedCommandIsNotRemembered) {
    Table t;
    for (uint8_t i = 0; i < SLOTS; i++) t.add(command(0x0100 + i, i, false), i);
    EXPECT_TRUE(t.add(command(0x0200, 50, false), 100));
    PendingCommand out;
    EXPECT_FALSE(t.pending.takeLate(0x0100, 0, 200, out));
}

TEST(PendingLate, AFullMemoryForgetsTheCommandGivenUpFirst) {
    PendingCommands pending(SLOTS, 3);
    PendingCommand evicted;
    PendingCommand out;
    for (uint8_t i = 1; i <= 4; i++) {
        PendingCommand c = command(0x0100 + i, i, false);
        pending.add(c, 0, evicted);
        ASSERT_TRUE(pending.giveUp(0x0100 + i, i, 1000U * i, out));
    }
    EXPECT_EQ(pending.lateSize(5000), 3U);
    EXPECT_FALSE(pending.takeLate(0x0101, 1, 5000, out));
    EXPECT_TRUE(pending.takeLate(0x0102, 2, 5000, out));
    EXPECT_TRUE(pending.takeLate(0x0103, 3, 5000, out));
    EXPECT_TRUE(pending.takeLate(0x0104, 4, 5000, out));
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
        PendingCommand added = command(0x0200, static_cast<uint8_t>(100 + expected), false);
        ASSERT_TRUE(t.pending.add(added, now++, evicted));
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
    PendingCommand added = command(0x0200, 99, false);
    ASSERT_TRUE(t.pending.add(added, start + 20, evicted));
    EXPECT_EQ(evicted.requestId, 0);
    added = command(0x0200, 98, false);
    ASSERT_TRUE(t.pending.add(added, start + 21, evicted));
    EXPECT_EQ(evicted.requestId, 1);
}

TEST(PendingCommands, TheTimesWorkAcrossAClockWrap) {
    Table t;
    const uint32_t start = 0xFFFFFFF0U;
    t.add(command(0x0B12, 1, true), start);
    t.sweep(start + SHORT_A - 1);
    EXPECT_TRUE(t.retransmit.empty());
    t.sweep(start + SHORT_A);
    EXPECT_EQ(t.retransmit.size(), 1U);
    t.sweep(start + SHORT_SIGNED_GIVE_UP);
    ASSERT_EQ(t.expired.size(), 1U);
    PendingCommand out;
    EXPECT_TRUE(
        t.pending.takeLate(0x0B12, 1, start + SHORT_SIGNED_GIVE_UP + COMMAND_LATE_REPLY_MS, out));
}

TEST(PendingRequestId, CountsUpAndWraps) {
    PendingCommands pending(SLOTS);
    EXPECT_EQ(pending.nextRequestId(0), 1);
    EXPECT_EQ(pending.nextRequestId(0), 2);
    for (int i = 0; i < 253; i++) pending.nextRequestId(0);
    EXPECT_EQ(pending.nextRequestId(0), 0);
    EXPECT_EQ(pending.nextRequestId(0), 1);
}

TEST(PendingRequestId, SkipsTheIdsOfWaitingCommands) {
    Table t;
    const AttemptTiming longWait = timing(0, 3, 90000);
    uint8_t first = t.pending.nextRequestId(0);
    t.add(command(0x0B12, first, true, longWait), 0);
    uint8_t second = t.pending.nextRequestId(0);
    t.add(command(0x0C40, second, true, longWait), 0);
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 2);
    // 254 more commands are answered at once: the counter comes round to the waiting ids.
    for (int i = 0; i < 254; i++) {
        uint8_t id = t.pending.nextRequestId(0);
        EXPECT_NE(id, first);
        EXPECT_NE(id, second);
    }
    EXPECT_EQ(t.pending.nextRequestId(0), 3);
    // A freed id is used again, and its reply goes to the new command only.
    PendingCommand out;
    ASSERT_TRUE(t.pending.take(0x0B12, first, out));
    for (int i = 0; i < 253; i++) t.pending.nextRequestId(0);
    EXPECT_EQ(t.pending.nextRequestId(0), first);
    t.add(command(0x0D07, first, false), 5000);
    EXPECT_FALSE(t.pending.take(0x0B12, first, out));
    ASSERT_TRUE(t.pending.take(0x0D07, first, out));
    EXPECT_EQ(out.sentMs, 5000U);
}

TEST(PendingRequestId, SkipsTheIdsOfCommandsRememberedForALateReply) {
    Table t;
    uint8_t first = t.pending.nextRequestId(0);
    ASSERT_EQ(first, 1);
    t.add(command(0x0B12, first, false), 0);
    t.sweep(SHORT_A);
    ASSERT_EQ(t.expired.size(), 1U);
    const uint32_t expiredAt = SHORT_A;
    for (int i = 0; i < 600; i++) EXPECT_NE(t.pending.nextRequestId(expiredAt + 1000), first);
    // The last moment a late reply is still matched.
    for (int i = 0; i < 300; i++) {
        EXPECT_NE(t.pending.nextRequestId(expiredAt + COMMAND_LATE_REPLY_MS), first);
    }
    // After ten minutes the id is free again.
    bool reused = false;
    for (int i = 0; i < 256; i++) {
        reused = reused || t.pending.nextRequestId(expiredAt + COMMAND_LATE_REPLY_MS + 1) == first;
    }
    EXPECT_TRUE(reused);
}

TEST(PendingRequestId, AMatchedLateReplyFreesItsId) {
    Table t;
    uint8_t first = t.pending.nextRequestId(0);
    t.add(command(0x0B12, first, false), 0);
    t.sweep(SHORT_A);
    PendingCommand out;
    ASSERT_TRUE(t.pending.takeLate(0x0B12, first, SHORT_A + 5000, out));
    bool reused = false;
    for (int i = 0; i < 256; i++) {
        reused = reused || t.pending.nextRequestId(SHORT_A + 6000) == first;
    }
    EXPECT_TRUE(reused);
}

TEST(PendingCommands, ZeroSlotsStillHoldsOneCommand) {
    PendingCommands pending(0, 0);
    PendingCommand evicted;
    PendingCommand one = command(1, 1, false);
    PendingCommand two = command(2, 2, false);
    EXPECT_FALSE(pending.add(one, 0, evicted));
    EXPECT_TRUE(pending.add(two, 1, evicted));
    EXPECT_EQ(evicted.dst, 1);
    EXPECT_EQ(pending.size(), 1U);
    PendingCommand out;
    ASSERT_TRUE(pending.giveUp(2, 2, 10, out));
    EXPECT_TRUE(pending.takeLate(2, 2, 20, out));
}

TEST(CommandAck, LinesHaveTheProtocolFormat) {
    PendingCommands pending(SLOTS);
    PendingCommand evicted;
    // Two hops, S = 15.2 s, the slot 4.3 s away: A1 = 4.3 + 45.6 + 2 = 51.9 s.
    PendingCommand c = command(0x0B12, 3, true, timing(4300, 2, 15200));
    c.originRequestId = 17;
    c.expectedMs = expectedReplyMs(c.timing, false, 0);
    pending.add(c, 100000, evicted);
    // Give-up: 51.9 + (15.2 + 45.6 + 2) = 114.7 s. Expected: 4.3 + 30.4 = 34.7 s.
    EXPECT_EQ(formatQueuedAck(c, 100000), "17 Q 115 35 2 15200");
    // The same request again 10 s later gets the remaining times.
    EXPECT_EQ(formatQueuedAck(c, 110000), "17 Q 105 25 2 15200");
    EXPECT_EQ(formatQueuedAck(c, 300000), "17 Q 0 0 2 15200");

    PendingCommand second;
    ASSERT_TRUE(pending.secondAttempt(0x0B12, 3, 152000, timing(9100, 2, 15200), 39500, second));
    // A2 = 9.1 + 45.6 + 2 = 56.7 s.
    EXPECT_EQ(formatRetransmitAck(second, 152000), "17 R 57");
    EXPECT_EQ(formatQueuedAck(second, 152000), "17 Q 57 40 2 15200");
    EXPECT_EQ(formatDoneAck(second, 171250), "17 D 71250 2");
    EXPECT_EQ(formatDoneAck(c, 131250), "17 D 31250 1");
    for (const std::string& line : {formatQueuedAck(c, 100000), formatRetransmitAck(second, 152000),
                                    formatDoneAck(second, 171250)}) {
        EXPECT_LE(line.size(), 40U);
    }
}

TEST(CommandAck, TheLongestLineFitsInFortyBytes) {
    PendingCommand c = command(1, 1, true, timing(0, 255, 4000000));
    c.originRequestId = 255;
    c.giveUpMs = 0xFFFFFFFF;
    c.expectedMs = 0xFFFFFFFF;
    EXPECT_LE(formatQueuedAck(c, 0).size(), 40U);
    EXPECT_LE(formatDoneAck(c, 0xFFFFFFFF).size(), 40U);
}

// One signed command for a node two hops away, from the moment the gateway takes it to a
// reply that arrives after the timeout.
TEST(CommandAck, WalksThroughAcceptedRetransmittedTimedOutAndLateReply) {
    PendingCommands pending(SLOTS);
    PendingCommand evicted;
    std::vector<PendingCommand> retransmit;
    std::vector<PendingCommand> expired;

    PendingCommand c = command(0x0B12, pending.nextRequestId(0), true, timing(4300, 2, 15200));
    c.originRequestId = 17;
    c.expectedMs = expectedReplyMs(c.timing, false, 0);
    pending.add(c, 0, evicted);
    EXPECT_EQ(formatQueuedAck(c, 0), "17 Q 115 35 2 15200");

    pending.sweep(51000, retransmit, expired);
    EXPECT_TRUE(retransmit.empty());
    pending.sweep(52000, retransmit, expired);
    ASSERT_EQ(retransmit.size(), 1U);
    PendingCommand second;
    AttemptTiming again = timing(9100, 2, 15200);
    ASSERT_TRUE(pending.secondAttempt(0x0B12, c.requestId, 52000, again,
                                      expectedReplyMs(again, false, 0), second));
    EXPECT_EQ(formatRetransmitAck(second, 52000), "17 R 57");

    pending.sweep(108000, retransmit, expired);
    EXPECT_TRUE(expired.empty());
    pending.sweep(109000, retransmit, expired);
    ASSERT_EQ(expired.size(), 1U);
    uint32_t waitedS = (expired[0].waitedMs(109000) + 500) / 1000;
    EXPECT_EQ(formatReplyPayload(expired[0].originRequestId,
                                 CommandResult(CommandStatus::TIMEOUT, std::to_string(waitedS))),
              "17 E8 109");

    PendingCommand late;
    ASSERT_TRUE(pending.takeLate(0x0B12, c.requestId, 140500, late));
    EXPECT_EQ(formatReplyPayload(late.originRequestId, CommandResult("OK install 600 s")),
              "17 OK install 600 s");
    EXPECT_EQ(formatDoneAck(late, 140500), "17 D 140500 2");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
