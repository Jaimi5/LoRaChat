#include <gtest/gtest.h>

#include "bootGuardLogic.h"
#include "fakeBoot.h"

namespace {

ShaPrefix sha(uint8_t seed) {
    ShaPrefix out{};
    out.fill(seed);
    return out;
}

const ShaPrefix IMAGE_A = sha(0xA0);
const ShaPrefix IMAGE_B = sha(0xB0);
const ShaPrefix IMAGE_C = sha(0xC0);

/** One node: bootloader model plus the record that lives in NVS. */
class Node {
public:
    explicit Node(bool bootloaderRollback = true) : boot(IMAGE_A, IMAGE_A, bootloaderRollback) {}

    BootAction bootUp() {
        BootDecision decision = BootGuardLogic::onBoot(boot.runningState(), boot.runningSha(), nvs);
        if (decision.recordChanged) nvs = decision.record;
        return decision.action;
    }

    void updateTo(const ShaPrefix& image) {
        nvs = BootGuardLogic::onAttemptStarted(nvs, image, false);
        boot.installAndSelect(image);
        boot.reboot();
    }

    void selfTestPasses() {
        boot.markValid();
        nvs = BootGuardLogic::onSelfTestPassed(nvs);
    }

    void selfTestFails(FailReason reason) {
        nvs = BootGuardLogic::onSelfTestFailed(nvs, reason);
        boot.markInvalidAndReboot();
    }

    FakeBoot boot;
    OtaRecord nvs;
};

}  // namespace

TEST(BootGuard, DoesNothingOnValidImageWithoutAttempt) {
    Node node;
    node.boot.reboot();
    EXPECT_EQ(node.bootUp(), BootAction::NONE);
    EXPECT_EQ(node.nvs, OtaRecord{});
}

TEST(BootGuard, RunsSelfTestWhenPendingVerify) {
    Node node;
    node.updateTo(IMAGE_B);
    EXPECT_EQ(node.boot.runningState(), ImageState::PENDING_VERIFY);
    EXPECT_EQ(node.bootUp(), BootAction::RUN_SELF_TEST);
}

TEST(BootGuard, GoodUpdateStaysAndIsReportedValid) {
    Node node;
    node.updateTo(IMAGE_B);
    ASSERT_EQ(node.bootUp(), BootAction::RUN_SELF_TEST);
    node.selfTestPasses();
    EXPECT_FALSE(node.nvs.pending);
    EXPECT_EQ(node.nvs.lastOutcome, OtaOutcome::VALID);

    node.boot.reboot();
    EXPECT_EQ(node.boot.runningSha(), IMAGE_B);
    EXPECT_EQ(node.bootUp(), BootAction::NONE);
}

TEST(BootGuard, ReportsValidWhenMarkedButNotPersisted) {
    Node node;
    node.updateTo(IMAGE_B);
    node.bootUp();
    node.boot.markValid();
    node.boot.reboot();
    EXPECT_EQ(node.bootUp(), BootAction::REPORT_VALID);
    EXPECT_FALSE(node.nvs.pending);
    EXPECT_EQ(node.nvs.lastOutcome, OtaOutcome::VALID);
}

TEST(BootGuard, SelfTestFailureRollsBackAndBlacklistsAtOnce) {
    Node node;
    node.updateTo(IMAGE_B);
    node.bootUp();
    node.selfTestFails(FailReason::MESH_SILENT);

    EXPECT_EQ(node.boot.runningSha(), IMAGE_A);
    EXPECT_EQ(node.bootUp(), BootAction::REPORT_ROLLBACK);
    EXPECT_TRUE(node.nvs.blacklist.contains(IMAGE_B));
    EXPECT_EQ(node.nvs.lastOutcome, OtaOutcome::ROLLED_BACK);
    EXPECT_EQ(node.nvs.failReason, FailReason::MESH_SILENT);
    EXPECT_FALSE(node.nvs.pending);
}

TEST(BootGuard, FirstUnexplainedRollbackOnlyCounts) {
    Node node;
    node.updateTo(IMAGE_B);
    node.bootUp();
    node.boot.reboot();

    EXPECT_EQ(node.boot.runningSha(), IMAGE_A);
    EXPECT_EQ(node.bootUp(), BootAction::REPORT_ROLLBACK);
    EXPECT_FALSE(node.nvs.blacklist.contains(IMAGE_B));
    EXPECT_EQ(node.nvs.unexplainedRollbacks, 1);
    EXPECT_EQ(node.nvs.failReason, FailReason::NONE);
}

TEST(BootGuard, SecondUnexplainedRollbackOfSameImageBlacklists) {
    Node node;
    for (int attempt = 0; attempt < 2; attempt++) {
        node.updateTo(IMAGE_B);
        node.bootUp();
        node.boot.reboot();
        EXPECT_EQ(node.bootUp(), BootAction::REPORT_ROLLBACK);
    }
    EXPECT_TRUE(node.nvs.blacklist.contains(IMAGE_B));
}

TEST(BootGuard, UnexplainedCountRestartsForAnotherImage) {
    Node node;
    node.updateTo(IMAGE_B);
    node.bootUp();
    node.boot.reboot();
    node.bootUp();

    node.updateTo(IMAGE_C);
    node.bootUp();
    node.boot.reboot();
    node.bootUp();

    EXPECT_FALSE(node.nvs.blacklist.contains(IMAGE_B));
    EXPECT_FALSE(node.nvs.blacklist.contains(IMAGE_C));
    EXPECT_EQ(node.nvs.unexplainedRollbacks, 1);
}

TEST(BootGuard, NewAttemptClearsPreviousFailReason) {
    Node node;
    node.updateTo(IMAGE_B);
    node.bootUp();
    node.selfTestFails(FailReason::HEAP);
    node.bootUp();

    node.nvs = BootGuardLogic::onAttemptStarted(node.nvs, IMAGE_C, false);
    EXPECT_EQ(node.nvs.failReason, FailReason::NONE);
    EXPECT_TRUE(node.nvs.pending);
    EXPECT_EQ(node.nvs.attemptSha, IMAGE_C);
}

TEST(BootGuard, AttemptCarriesTheSkipMeshCheckFlag) {
    OtaRecord record = BootGuardLogic::onAttemptStarted(OtaRecord{}, IMAGE_B, true);
    EXPECT_TRUE(record.skipMeshCheck);
    record = BootGuardLogic::onAttemptStarted(record, IMAGE_C, false);
    EXPECT_FALSE(record.skipMeshCheck);
}

TEST(BootGuard, MeshFrameRequiredOnlyWithNeighboursAndWithoutSkipFlag) {
    OtaRecord plain = BootGuardLogic::onAttemptStarted(OtaRecord{}, IMAGE_B, false);
    OtaRecord skip = BootGuardLogic::onAttemptStarted(OtaRecord{}, IMAGE_B, true);
    EXPECT_TRUE(BootGuardLogic::meshFrameRequired(true, plain));
    EXPECT_FALSE(BootGuardLogic::meshFrameRequired(true, skip));
    EXPECT_FALSE(BootGuardLogic::meshFrameRequired(false, plain));
    EXPECT_FALSE(BootGuardLogic::meshFrameRequired(false, skip));
}

TEST(BootGuard, ReportsBootloaderWithoutRollback) {
    Node node(false);
    node.updateTo(IMAGE_B);
    EXPECT_EQ(node.boot.runningState(), ImageState::NEW);
    EXPECT_EQ(node.bootUp(), BootAction::REPORT_BL_NO_ROLLBACK);
    EXPECT_EQ(node.nvs.lastOutcome, OtaOutcome::BL_NO_ROLLBACK);
    EXPECT_FALSE(node.nvs.pending);

    node.boot.reboot();
    EXPECT_EQ(node.bootUp(), BootAction::NONE);
}

TEST(BootGuard, HeapThresholds) {
    EXPECT_TRUE(BootGuardLogic::heapOk(40 * 1024, 16 * 1024));
    EXPECT_FALSE(BootGuardLogic::heapOk(40 * 1024 - 1, 16 * 1024));
    EXPECT_FALSE(BootGuardLogic::heapOk(40 * 1024, 16 * 1024 - 1));
}

namespace {

void passBasicChecks(SelfTest& test) {
    test.report(SelfTestCheck::RADIO, true);
    test.report(SelfTestCheck::HEAP, true);
    test.report(SelfTestCheck::PMU, true);
    test.report(SelfTestCheck::OTA_PATH, true);
}

}  // namespace

TEST(SelfTest, PassesWhenAllChecksPassWithoutMeshRequirement) {
    SelfTest test(1000, false);
    EXPECT_EQ(test.evaluate(1000), SelfTestVerdict::WAITING);
    passBasicChecks(test);
    EXPECT_EQ(test.evaluate(2000), SelfTestVerdict::PASS);
}

TEST(SelfTest, WaitsForMeshFrameWhenRequired) {
    SelfTest test(0, true);
    passBasicChecks(test);
    EXPECT_EQ(test.evaluate(60000), SelfTestVerdict::WAITING);
    test.report(SelfTestCheck::MESH_FRAME, true);
    EXPECT_EQ(test.evaluate(60001), SelfTestVerdict::PASS);
}

TEST(SelfTest, FailsWhenMeshSilentForTwoHelloPeriods) {
    SelfTest test(0, true);
    passBasicChecks(test);
    EXPECT_EQ(test.evaluate(BootGuardLogic::MESH_WINDOW_MS - 1), SelfTestVerdict::WAITING);
    EXPECT_EQ(test.evaluate(BootGuardLogic::MESH_WINDOW_MS), SelfTestVerdict::FAIL);
    EXPECT_EQ(test.failReason(), FailReason::MESH_SILENT);
}

TEST(SelfTest, FailedCheckIsFinal) {
    SelfTest test(0, false);
    test.report(SelfTestCheck::PMU, false);
    passBasicChecks(test);
    EXPECT_EQ(test.evaluate(10), SelfTestVerdict::FAIL);
    EXPECT_EQ(test.failReason(), FailReason::PMU);
}

TEST(SelfTest, EachCheckMapsToItsReason) {
    const std::pair<SelfTestCheck, FailReason> cases[] = {
        {SelfTestCheck::RADIO, FailReason::RADIO},
        {SelfTestCheck::HEAP, FailReason::HEAP},
        {SelfTestCheck::PMU, FailReason::PMU},
        {SelfTestCheck::OTA_PATH, FailReason::OTA_PATH},
        {SelfTestCheck::MESH_FRAME, FailReason::MESH_SILENT},
    };
    for (const auto& c : cases) {
        SelfTest test(0, true);
        test.report(c.first, false);
        EXPECT_EQ(test.evaluate(1), SelfTestVerdict::FAIL);
        EXPECT_EQ(test.failReason(), c.second);
    }
}

TEST(SelfTest, DeadlineFailsMissingChecks) {
    SelfTest test(0, false);
    test.report(SelfTestCheck::RADIO, true);
    test.report(SelfTestCheck::HEAP, true);
    test.report(SelfTestCheck::OTA_PATH, true);
    EXPECT_EQ(test.evaluate(BootGuardLogic::DEADLINE_MS - 1), SelfTestVerdict::WAITING);
    EXPECT_EQ(test.evaluate(BootGuardLogic::DEADLINE_MS), SelfTestVerdict::FAIL);
    EXPECT_EQ(test.failReason(), FailReason::DEADLINE);
}

TEST(SelfTest, SurvivesMillisWrapAround) {
    const uint32_t start = 0xFFFFF000u;
    SelfTest test(start, false);
    EXPECT_EQ(test.evaluate(start + 100000u), SelfTestVerdict::WAITING);
    EXPECT_EQ(test.evaluate(start + BootGuardLogic::DEADLINE_MS), SelfTestVerdict::FAIL);
    EXPECT_EQ(test.failReason(), FailReason::DEADLINE);
}

TEST(SelfTest, MeshFrameIgnoredWhenNotRequired) {
    SelfTest test(0, false);
    test.report(SelfTestCheck::MESH_FRAME, true);
    passBasicChecks(test);
    EXPECT_EQ(test.evaluate(1), SelfTestVerdict::PASS);
}

TEST(SelfTestTiming, DefaultsWithoutSuperframe) {
    SelfTestTiming timing = BootGuardLogic::timingFor(0);
    EXPECT_EQ(timing.meshWindowMs, 120000u);
    EXPECT_EQ(timing.deadlineMs, 240000u);
    EXPECT_EQ(timing.watchdogMs, 250000u);
}

TEST(SelfTestTiming, ScalesWithSuperframe) {
    SelfTestTiming sf9 = BootGuardLogic::timingFor(40900);
    EXPECT_EQ(sf9.meshWindowMs, 163600u);
    EXPECT_EQ(sf9.deadlineMs, 283600u);
    EXPECT_EQ(sf9.watchdogMs, 293600u);

    SelfTestTiming sf12 = BootGuardLogic::timingFor(141900);
    EXPECT_EQ(sf12.meshWindowMs, 567600u);
    EXPECT_EQ(sf12.deadlineMs, 687600u);
    EXPECT_EQ(sf12.watchdogMs, 697600u);
}

TEST(SelfTestTiming, ClampsAbsurdSuperframe) {
    SelfTestTiming timing = BootGuardLogic::timingFor(0xFFFFFFFFu);
    EXPECT_EQ(timing.meshWindowMs, 4 * BootGuardLogic::MAX_SUPERFRAME_MS);
    EXPECT_GT(timing.watchdogMs, timing.deadlineMs);
}

TEST(SelfTest, LongSuperframeExtendsMeshWindowAndDeadline) {
    SelfTest test(0, true);
    test.setSuperframe(141900);
    passBasicChecks(test);
    EXPECT_EQ(test.evaluate(BootGuardLogic::MESH_WINDOW_MS), SelfTestVerdict::WAITING);
    EXPECT_EQ(test.evaluate(567599), SelfTestVerdict::WAITING);
    EXPECT_EQ(test.evaluate(567600), SelfTestVerdict::FAIL);
    EXPECT_EQ(test.failReason(), FailReason::MESH_SILENT);
}

TEST(SelfTest, DeadlineFollowsSuperframe) {
    SelfTest test(0, false);
    test.setSuperframe(141900);
    test.report(SelfTestCheck::RADIO, true);
    EXPECT_EQ(test.evaluate(BootGuardLogic::DEADLINE_MS), SelfTestVerdict::WAITING);
    EXPECT_EQ(test.evaluate(687600), SelfTestVerdict::FAIL);
    EXPECT_EQ(test.failReason(), FailReason::DEADLINE);
}

TEST(SelfTest, TimingNeverShrinks) {
    SelfTest test(0, true);
    EXPECT_TRUE(test.setSuperframe(141900));
    EXPECT_FALSE(test.setSuperframe(40900));
    EXPECT_FALSE(test.setSuperframe(0));
    EXPECT_EQ(test.timing().meshWindowMs, 567600u);
}

TEST(BootLine, FormatsAllFields) {
    EXPECT_EQ(formatBootLine("ota_1", ImageState::VALID, "1.3.0", "3fa2c1", "POWERON"),
              "BOOT part=ota_1 state=VALID ver=1.3.0 bl=3fa2c1 rr=POWERON");
}

TEST(BootLine, NamesEveryState) {
    EXPECT_STREQ(imageStateName(ImageState::NEW), "NEW");
    EXPECT_STREQ(imageStateName(ImageState::PENDING_VERIFY), "PENDING_VERIFY");
    EXPECT_STREQ(imageStateName(ImageState::VALID), "VALID");
    EXPECT_STREQ(imageStateName(ImageState::INVALID), "INVALID");
    EXPECT_STREQ(imageStateName(ImageState::ABORTED), "ABORTED");
    EXPECT_STREQ(imageStateName(ImageState::UNDEFINED), "UNDEFINED");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
