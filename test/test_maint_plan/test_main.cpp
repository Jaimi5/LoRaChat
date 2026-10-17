#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "maintWindowPlan.h"

namespace {
constexpr uint32_t T0 = 1000;
}

TEST(MaintWindowPlan, OpensAPullWindow) {
    MaintWindowPlan plan;
    EXPECT_EQ(plan.open(WindowKind::PULL, 3600, T0), OpenDecision::OPENED);
    EXPECT_EQ(plan.active(T0), WindowKind::PULL);
    EXPECT_EQ(plan.remaining(T0 + 600), 3000u);
    EXPECT_EQ(plan.active(T0 + 3600), WindowKind::NONE);
    EXPECT_EQ(plan.remaining(T0 + 3600), 0u);
    EXPECT_EQ(plan.countedInLastDay(T0), 1u);
}

TEST(MaintWindowPlan, RefusesDurationsOutsideTheLimits) {
    MaintWindowPlan plan;
    EXPECT_EQ(plan.open(WindowKind::PULL, 0, T0), OpenDecision::REFUSED_DURATION);
    EXPECT_EQ(plan.open(WindowKind::PULL, MaintWindowPlan::MAX_PULL_S + 1, T0),
              OpenDecision::REFUSED_DURATION);
    EXPECT_EQ(plan.open(WindowKind::AP, MaintWindowPlan::MAX_AP_S + 1, T0),
              OpenDecision::REFUSED_DURATION);
    EXPECT_EQ(plan.open(WindowKind::NONE, 60, T0), OpenDecision::REFUSED_DURATION);
    EXPECT_EQ(plan.countedInLastDay(T0), 0u);
    EXPECT_EQ(plan.open(WindowKind::AP, MaintWindowPlan::MAX_AP_S, T0), OpenDecision::OPENED);
}

TEST(MaintWindowPlan, SameKindExtendsWithoutCounting) {
    MaintWindowPlan plan;
    plan.open(WindowKind::PULL, 600, T0);
    EXPECT_EQ(plan.open(WindowKind::PULL, 1800, T0 + 300), OpenDecision::EXTENDED);
    EXPECT_EQ(plan.remaining(T0 + 300), 1800u);
    EXPECT_EQ(plan.countedInLastDay(T0 + 300), 1u);
    // A shorter request never shortens the open window.
    EXPECT_EQ(plan.open(WindowKind::PULL, 60, T0 + 400), OpenDecision::EXTENDED);
    EXPECT_EQ(plan.remaining(T0 + 400), 1700u);
}

TEST(MaintWindowPlan, OtherKindReplacesTheWindowAndCounts) {
    MaintWindowPlan plan;
    plan.open(WindowKind::PULL, 3600, T0);
    EXPECT_EQ(plan.open(WindowKind::AP, 600, T0 + 100), OpenDecision::OPENED);
    EXPECT_EQ(plan.active(T0 + 100), WindowKind::AP);
    EXPECT_EQ(plan.remaining(T0 + 100), 600u);
    EXPECT_EQ(plan.countedInLastDay(T0 + 100), 2u);
}

TEST(MaintWindowPlan, AllowsEightWindowsPerDay) {
    MaintWindowPlan plan;
    uint32_t now = T0;
    for (size_t i = 0; i < MaintWindowPlan::MAX_PER_DAY; i++) {
        ASSERT_EQ(plan.open(WindowKind::PULL, 60, now), OpenDecision::OPENED) << i;
        now += 120;
    }
    EXPECT_EQ(plan.open(WindowKind::PULL, 60, now), OpenDecision::REFUSED_LIMIT);
    EXPECT_EQ(plan.open(WindowKind::AP, 60, now), OpenDecision::REFUSED_LIMIT);
    EXPECT_EQ(plan.active(now), WindowKind::NONE);

    // The oldest window leaves the 24 h span, so one more may open.
    uint32_t later = T0 + MaintWindowPlan::DAY_S;
    EXPECT_EQ(plan.open(WindowKind::PULL, 60, later), OpenDecision::OPENED);
    EXPECT_EQ(plan.open(WindowKind::AP, 60, later), OpenDecision::REFUSED_LIMIT);
}

TEST(MaintWindowPlan, ExtendingIsAllowedAtTheLimit) {
    MaintWindowPlan plan;
    uint32_t now = T0;
    for (size_t i = 0; i < MaintWindowPlan::MAX_PER_DAY; i++) {
        plan.open(WindowKind::PULL, 600, now);
        plan.close();
        now += 10;
    }
    ASSERT_EQ(plan.open(WindowKind::PULL, 600, now), OpenDecision::REFUSED_LIMIT);
    MaintWindowPlan open;
    for (size_t i = 0; i + 1 < MaintWindowPlan::MAX_PER_DAY; i++) {
        open.open(WindowKind::AP, 60, T0);
        open.close();
    }
    ASSERT_EQ(open.open(WindowKind::PULL, 600, T0 + 1), OpenDecision::OPENED);
    ASSERT_EQ(open.countedInLastDay(T0 + 1), MaintWindowPlan::MAX_PER_DAY);
    EXPECT_EQ(open.open(WindowKind::PULL, 1200, T0 + 2), OpenDecision::EXTENDED);
}

// Case 41: an access point closes 2 min after the result, and never later than planned.
TEST(MaintWindowPlan, AccessPointClosesTwoMinutesAfterTheResult) {
    MaintWindowPlan plan;
    plan.open(WindowKind::AP, 1800, T0);
    plan.onResult(T0 + 300);
    EXPECT_EQ(plan.remaining(T0 + 300), MaintWindowPlan::RESULT_GRACE_S);
    EXPECT_EQ(plan.active(T0 + 300 + MaintWindowPlan::RESULT_GRACE_S), WindowKind::NONE);

    MaintWindowPlan late;
    late.open(WindowKind::AP, 600, T0);
    late.onResult(T0 + 550);
    EXPECT_EQ(late.remaining(T0 + 550), 50u);

    MaintWindowPlan pull;
    pull.open(WindowKind::PULL, 600, T0);
    pull.onResult(T0 + 10);
    EXPECT_EQ(pull.remaining(T0 + 10), 590u);
}

TEST(MaintWindowPlan, ClosingKeepsTheCount) {
    MaintWindowPlan plan;
    plan.open(WindowKind::AP, 600, T0);
    plan.close();
    EXPECT_EQ(plan.active(T0 + 1), WindowKind::NONE);
    EXPECT_EQ(plan.countedInLastDay(T0 + 1), 1u);
}

// A window survives a reboot: the node clock continues from the stored value.
TEST(MaintWindowPlan, ResumesAfterAReboot) {
    MaintWindowPlan plan;
    plan.open(WindowKind::AP, 1800, T0);
    std::vector<uint8_t> blob = plan.encode();

    MaintWindowPlan restored;
    ASSERT_TRUE(MaintWindowPlan::decode(blob.data(), blob.size(), restored));
    EXPECT_EQ(restored, plan);
    EXPECT_EQ(restored.active(T0 + 400), WindowKind::AP);
    EXPECT_EQ(restored.remaining(T0 + 400), 1400u);
    EXPECT_EQ(restored.countedInLastDay(T0 + 400), 1u);
}

// Case 32: the node clock may wrap; ages are computed modulo 2^32.
TEST(MaintWindowPlan, HandlesClockWrap) {
    MaintWindowPlan plan;
    uint32_t nearWrap = UINT32_MAX - 100;
    plan.open(WindowKind::PULL, 600, nearWrap);
    EXPECT_EQ(plan.active(nearWrap + 300), WindowKind::PULL);
    EXPECT_EQ(plan.remaining(nearWrap + 300), 300u);
    EXPECT_EQ(plan.countedInLastDay(nearWrap + 300), 1u);
    EXPECT_EQ(plan.countedInLastDay(nearWrap + MaintWindowPlan::DAY_S), 0u);
}

TEST(MaintWindowPlan, SavesTheClockOftenEnough) {
    MaintWindowPlan idle;
    EXPECT_FALSE(idle.clockSaveDue(T0 + 100000, T0));

    MaintWindowPlan open;
    open.open(WindowKind::PULL, 3600, T0);
    EXPECT_FALSE(open.clockSaveDue(T0 + MaintWindowPlan::SAVE_IN_WINDOW_S - 1, T0));
    EXPECT_TRUE(open.clockSaveDue(T0 + MaintWindowPlan::SAVE_IN_WINDOW_S, T0));

    open.close();
    EXPECT_FALSE(open.clockSaveDue(T0 + MaintWindowPlan::SAVE_WHILE_COUNTING_S - 1, T0));
    EXPECT_TRUE(open.clockSaveDue(T0 + MaintWindowPlan::SAVE_WHILE_COUNTING_S, T0));
    uint32_t dayLater = T0 + MaintWindowPlan::DAY_S + 1;
    EXPECT_FALSE(open.clockSaveDue(dayLater, dayLater - 2 * MaintWindowPlan::SAVE_WHILE_COUNTING_S));
}

TEST(MaintWindowPlan, DecodeRejectsOtherBlobs) {
    MaintWindowPlan plan;
    plan.open(WindowKind::PULL, 60, T0);
    std::vector<uint8_t> blob = plan.encode();
    MaintWindowPlan out;
    EXPECT_FALSE(MaintWindowPlan::decode(blob.data(), blob.size() - 1, out));
    std::vector<uint8_t> badMagic = blob;
    badMagic[0] ^= 0xFF;
    EXPECT_FALSE(MaintWindowPlan::decode(badMagic.data(), badMagic.size(), out));
    std::vector<uint8_t> badKind = blob;
    badKind[4] = 9;
    EXPECT_FALSE(MaintWindowPlan::decode(badKind.data(), badKind.size(), out));
    EXPECT_FALSE(MaintWindowPlan::decode(nullptr, 0, out));
}

TEST(MaintWindowPlan, DecodesANewerBlobWithAppendedFields) {
    MaintWindowPlan plan;
    plan.open(WindowKind::AP, 60, T0);
    std::vector<uint8_t> blob = plan.encode();
    blob[2] = 2;
    blob.push_back(0xAA);
    blob[3] = static_cast<uint8_t>(blob.size());
    MaintWindowPlan out;
    ASSERT_TRUE(MaintWindowPlan::decode(blob.data(), blob.size(), out));
    EXPECT_EQ(out, plan);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
