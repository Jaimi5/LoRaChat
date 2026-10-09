#include "bootGuardLogic.h"

#include <algorithm>

namespace {

constexpr uint8_t checkBit(SelfTestCheck check) {
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(check));
}

constexpr uint8_t BASIC_CHECKS = checkBit(SelfTestCheck::RADIO) | checkBit(SelfTestCheck::HEAP) |
                                 checkBit(SelfTestCheck::PMU) | checkBit(SelfTestCheck::OTA_PATH);

FailReason reasonFor(SelfTestCheck check) {
    switch (check) {
        case SelfTestCheck::RADIO:
            return FailReason::RADIO;
        case SelfTestCheck::HEAP:
            return FailReason::HEAP;
        case SelfTestCheck::PMU:
            return FailReason::PMU;
        case SelfTestCheck::OTA_PATH:
            return FailReason::OTA_PATH;
        case SelfTestCheck::MESH_FRAME:
            return FailReason::MESH_SILENT;
    }
    return FailReason::DEADLINE;
}

BootDecision resolved(BootAction action, OtaRecord record) {
    BootDecision decision;
    decision.action = action;
    decision.record = record;
    decision.recordChanged = true;
    return decision;
}

}  // namespace

constexpr uint32_t BootGuardLogic::DEADLINE_MS;
constexpr uint32_t BootGuardLogic::MESH_WINDOW_MS;
constexpr uint32_t BootGuardLogic::MESH_WINDOW_SUPERFRAMES;
constexpr uint32_t BootGuardLogic::DEADLINE_MARGIN_MS;
constexpr uint32_t BootGuardLogic::WATCHDOG_MARGIN_MS;
constexpr uint32_t BootGuardLogic::MAX_SUPERFRAME_MS;
constexpr uint32_t BootGuardLogic::MIN_FREE_HEAP;
constexpr uint32_t BootGuardLogic::MIN_LARGEST_BLOCK;
constexpr uint8_t BootGuardLogic::MAX_UNEXPLAINED_ROLLBACKS;

BootDecision BootGuardLogic::onBoot(ImageState state, const ShaPrefix& runningSha,
                                    const OtaRecord& stored) {
    if (state == ImageState::PENDING_VERIFY) {
        BootDecision decision;
        decision.action = BootAction::RUN_SELF_TEST;
        decision.record = stored;
        return decision;
    }

    if (!stored.pending) {
        BootDecision decision;
        decision.record = stored;
        return decision;
    }

    OtaRecord record = stored;
    record.pending = false;

    if (runningSha == stored.attemptSha) {
        if (state == ImageState::NEW) {
            record.lastOutcome = OtaOutcome::BL_NO_ROLLBACK;
            return resolved(BootAction::REPORT_BL_NO_ROLLBACK, record);
        }
        record.lastOutcome = OtaOutcome::VALID;
        record.unexplainedRollbacks = 0;
        return resolved(BootAction::REPORT_VALID, record);
    }

    record.lastOutcome = OtaOutcome::ROLLED_BACK;
    if (stored.failReason != FailReason::NONE) {
        record.blacklist.add(stored.attemptSha);
    } else {
        record.unexplainedRollbacks++;
        if (record.unexplainedRollbacks >= MAX_UNEXPLAINED_ROLLBACKS) {
            record.blacklist.add(stored.attemptSha);
        }
    }
    return resolved(BootAction::REPORT_ROLLBACK, record);
}

uint32_t BootGuardLogic::rtcWatchdogSettingMs(uint32_t timeoutMs, uint32_t nominalHz,
                                             uint32_t calQ19) {
    if (calQ19 == 0 || nominalHz == 0) return timeoutMs;
    // actual Hz = 10^6 * 2^19 / calQ19; setting = timeoutMs * actual Hz / nominal Hz.
    uint64_t setting = (static_cast<uint64_t>(timeoutMs) * (1000000ull << 19)) /
                       (static_cast<uint64_t>(calQ19) * nominalHz);
    return setting > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(setting);
}

OtaRecord BootGuardLogic::onAttemptStarted(const OtaRecord& stored, const ShaPrefix& sha,
                                           bool skipMeshCheck) {
    OtaRecord record = stored;
    if (record.attemptSha != sha) record.unexplainedRollbacks = 0;
    record.attemptSha = sha;
    record.pending = true;
    record.failReason = FailReason::NONE;
    record.skipMeshCheck = skipMeshCheck;
    return record;
}

bool BootGuardLogic::meshFrameRequired(bool hadNeighbours, const OtaRecord& record) {
    return hadNeighbours && !record.skipMeshCheck;
}

OtaRecord BootGuardLogic::onSelfTestPassed(const OtaRecord& stored) {
    OtaRecord record = stored;
    record.pending = false;
    record.unexplainedRollbacks = 0;
    record.failReason = FailReason::NONE;
    record.lastOutcome = OtaOutcome::VALID;
    return record;
}

OtaRecord BootGuardLogic::onSelfTestFailed(const OtaRecord& stored, FailReason reason) {
    OtaRecord record = stored;
    record.failReason = reason;
    return record;
}

bool BootGuardLogic::heapOk(uint32_t freeBytes, uint32_t largestBlock) {
    return freeBytes >= MIN_FREE_HEAP && largestBlock >= MIN_LARGEST_BLOCK;
}

SelfTestTiming BootGuardLogic::timingFor(uint32_t superframeMs) {
    uint32_t superframe = std::min(superframeMs, MAX_SUPERFRAME_MS);
    uint32_t meshWindow = std::max(MESH_WINDOW_MS, superframe * MESH_WINDOW_SUPERFRAMES);
    uint32_t deadline = std::max(DEADLINE_MS, meshWindow + DEADLINE_MARGIN_MS);
    return {meshWindow, deadline, deadline + WATCHDOG_MARGIN_MS};
}

SelfTest::SelfTest(uint32_t startMs, bool requireMeshFrame)
    : startMs_(startMs),
      requireMeshFrame_(requireMeshFrame),
      timing_(BootGuardLogic::timingFor(0)) {}

bool SelfTest::setSuperframe(uint32_t superframeMs) {
    SelfTestTiming candidate = BootGuardLogic::timingFor(superframeMs);
    if (candidate.meshWindowMs <= timing_.meshWindowMs) return false;
    timing_ = candidate;
    return true;
}

void SelfTest::report(SelfTestCheck check, bool ok) {
    if (failReason_ != FailReason::NONE) return;
    if (!ok) {
        failReason_ = reasonFor(check);
        return;
    }
    passed_ |= checkBit(check);
}

SelfTestVerdict SelfTest::evaluate(uint32_t nowMs) {
    if (failReason_ != FailReason::NONE) return SelfTestVerdict::FAIL;

    uint8_t required = BASIC_CHECKS;
    if (requireMeshFrame_) required |= checkBit(SelfTestCheck::MESH_FRAME);
    if ((passed_ & required) == required) return SelfTestVerdict::PASS;

    uint32_t elapsed = nowMs - startMs_;
    bool meshMissing = requireMeshFrame_ && !(passed_ & checkBit(SelfTestCheck::MESH_FRAME));
    if (meshMissing && elapsed >= timing_.meshWindowMs) {
        failReason_ = FailReason::MESH_SILENT;
        return SelfTestVerdict::FAIL;
    }
    if (elapsed >= timing_.deadlineMs) {
        failReason_ = FailReason::DEADLINE;
        return SelfTestVerdict::FAIL;
    }
    return SelfTestVerdict::WAITING;
}

std::string formatBootLine(const std::string& partition, ImageState state,
                           const std::string& version, const std::string& bootloaderHash,
                           const std::string& resetReason) {
    return "BOOT part=" + partition + " state=" + imageStateName(state) + " ver=" + version +
           " bl=" + bootloaderHash + " rr=" + resetReason;
}

const char* imageStateName(ImageState state) {
    switch (state) {
        case ImageState::NEW:
            return "NEW";
        case ImageState::PENDING_VERIFY:
            return "PENDING_VERIFY";
        case ImageState::VALID:
            return "VALID";
        case ImageState::INVALID:
            return "INVALID";
        case ImageState::ABORTED:
            return "ABORTED";
        case ImageState::UNDEFINED:
            return "UNDEFINED";
    }
    return "UNKNOWN";
}
