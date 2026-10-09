#pragma once

#include <cstdint>
#include <string>

#include "otaRecord.h"

/** Mirror of esp_ota_img_states_t without the IDF dependency. */
enum class ImageState : uint8_t {
    NEW,
    PENDING_VERIFY,
    VALID,
    INVALID,
    ABORTED,
    UNDEFINED,
};

/** What the boot guard has to do at this boot. */
enum class BootAction : uint8_t {
    NONE,
    RUN_SELF_TEST,
    REPORT_VALID,
    REPORT_ROLLBACK,
    REPORT_BL_NO_ROLLBACK,
};

/** Result of BootGuardLogic::onBoot(). */
struct BootDecision {
    BootAction action = BootAction::NONE;
    /** The record to persist when it differs from the stored one. */
    OtaRecord record;
    bool recordChanged = false;
};

/** Checks run by the self-test of an image in PENDING_VERIFY. */
enum class SelfTestCheck : uint8_t {
    RADIO,
    HEAP,
    PMU,
    OTA_PATH,
    MESH_FRAME,
};

/** Time limits of one self-test run, in milliseconds from the guard start. */
struct SelfTestTiming {
    /** A required mesh frame must be heard before this. */
    uint32_t meshWindowMs;
    /** Software deadline for the whole self-test. */
    uint32_t deadlineMs;
    /** RTC watchdog timeout, which also covers hangs with interrupts off. */
    uint32_t watchdogMs;
};

enum class SelfTestVerdict : uint8_t {
    WAITING,
    PASS,
    FAIL,
};

/**
 * @brief Pure decisions of the OTA boot guard.
 *
 * Holds no hardware state. The firmware wrapper feeds it the running image state, the stored
 * OtaRecord and the self-test results, and applies the decisions with the IDF calls.
 */
class BootGuardLogic {
public:
    static constexpr uint32_t DEADLINE_MS = 240000;
    static constexpr uint32_t MESH_WINDOW_MS = 120000;
    static constexpr uint32_t MESH_WINDOW_SUPERFRAMES = 4;
    static constexpr uint32_t DEADLINE_MARGIN_MS = 120000;
    static constexpr uint32_t WATCHDOG_MARGIN_MS = 10000;
    static constexpr uint32_t MAX_SUPERFRAME_MS = 600000;
    static constexpr uint32_t MIN_FREE_HEAP = 40 * 1024;
    static constexpr uint32_t MIN_LARGEST_BLOCK = 16 * 1024;
    static constexpr uint8_t MAX_UNEXPLAINED_ROLLBACKS = 2;

    /**
     * @brief Resolves the stored attempt at boot.
     * @param state State of the running image.
     * @param runningSha SHA prefix of the running image.
     * @param stored Record read from NVS (default-constructed if none).
     */
    static BootDecision onBoot(ImageState state, const ShaPrefix& runningSha,
                               const OtaRecord& stored);

    /**
     * @brief Marks an attempt to boot @p sha as pending, right before rebooting into it.
     *
     * The unexplained rollback count is kept when the same image is tried again.
     * @param skipMeshCheck the manifest flag that lets the image pass without mesh contact.
     */
    static OtaRecord onAttemptStarted(const OtaRecord& stored, const ShaPrefix& sha,
                                      bool skipMeshCheck);

    /**
     * @brief Whether the self-test of the pending image must see a mesh frame.
     * @param hadNeighbours the node has had mesh contact before.
     */
    static bool meshFrameRequired(bool hadNeighbours, const OtaRecord& record);

    /** @return the record after the self-test passed and the image was marked valid. */
    static OtaRecord onSelfTestPassed(const OtaRecord& stored);

    /** @return the record to persist right before asking for a rollback. */
    static OtaRecord onSelfTestFailed(const OtaRecord& stored, FailReason reason);

    /** @return true if the heap figures pass the self-test thresholds. */
    static bool heapOk(uint32_t freeBytes, uint32_t largestBlock);

    /**
     * @brief Self-test limits for a mesh with the given superframe.
     *
     * The mesh window is MESH_WINDOW_SUPERFRAMES superframes and never shorter than
     * MESH_WINDOW_MS. The deadline leaves DEADLINE_MARGIN_MS after it and is never shorter than
     * DEADLINE_MS. @p superframeMs = 0 means unknown.
     */
    static SelfTestTiming timingFor(uint32_t superframeMs);
};

/**
 * @brief Self-test state machine with the software deadline.
 *
 * Time is passed in as a millisecond counter that may wrap around.
 */
class SelfTest {
public:
    /**
     * @param startMs Time the guard started.
     * @param requireMeshFrame true if the node had neighbours before and the release does not
     *        skip the check.
     */
    SelfTest(uint32_t startMs, bool requireMeshFrame);

    /** Records the result of @p check. A failed check is final. */
    void report(SelfTestCheck check, bool ok);

    /**
     * @brief Adapts the limits to the mesh superframe. Limits only ever grow.
     * @return true if the limits changed.
     */
    bool setSuperframe(uint32_t superframeMs);

    /** @return the current limits. */
    const SelfTestTiming& timing() const { return timing_; }

    /** @return the verdict at @p nowMs. */
    SelfTestVerdict evaluate(uint32_t nowMs);

    /** @return the reason of a FAIL verdict, NONE otherwise. */
    FailReason failReason() const { return failReason_; }

private:
    uint32_t startMs_;
    bool requireMeshFrame_;
    SelfTestTiming timing_;
    uint8_t passed_ = 0;
    FailReason failReason_ = FailReason::NONE;
};

/**
 * @brief Formats the serial boot line, e.g.
 * `BOOT part=ota_1 state=VALID ver=1.3.0 bl=3fa2c1 rr=POWERON`.
 */
std::string formatBootLine(const std::string& partition, ImageState state,
                           const std::string& version, const std::string& bootloaderHash,
                           const std::string& resetReason);

/** @return the upper-case name of @p state. */
const char* imageStateName(ImageState state);
