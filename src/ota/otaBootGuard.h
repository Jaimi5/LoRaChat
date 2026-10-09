#pragma once

#include <Arduino.h>

#include <atomic>
#include <memory>
#include <mutex>

#include "bootGuardLogic.h"

#define TEST_IMAGE_CRASH 1
#define TEST_IMAGE_IRQ_HANG 2
#define TEST_IMAGE_LOOP_HANG 3
#define TEST_IMAGE_SELFTEST_FAIL 4

/**
 * @brief Decides at every boot whether the running image is kept.
 *
 * In PENDING_VERIFY it arms the RTC watchdog, runs the self-test with a software deadline and
 * either marks the image valid or asks the bootloader for a rollback. On every boot it resolves
 * the pending update attempt stored in NVS (namespace "lmota") and prints the BOOT line.
 */
class OtaBootGuard {
public:
    static OtaBootGuard& getInstance() {
        static OtaBootGuard instance;
        return instance;
    }

    /** Must be the first call in setup(). */
    void begin();

    /** Called by the services that own the radio and the PMU once they are initialised. */
    void reportCheck(SelfTestCheck check, bool ok);

    /** Called at the end of setup(). Samples the heap for the self-test. */
    void reportSetupDone();

    /** @return true while the running image waits for its self-test verdict. */
    bool isPendingVerify() const { return pendingVerify_; }

    /**
     * @return true once this boot has resolved an update attempt: the new image passed its
     *         self-test, or the previous one booted again after a rollback.
     */
    bool attemptResolved() const { return attemptResolved_; }

    /** @return the persisted OTA record. */
    OtaRecord record() const;

    /**
     * @brief Records that the image with @p sha boots next, so the next boot resolves the
     *        attempt. @p skipMeshCheck is the manifest flag that lets its self-test pass
     *        without mesh contact.
     */
    void recordAttempt(const ShaPrefix& sha, bool skipMeshCheck);

    /** @return the first 3 bytes of the SHA-256 of the bootloader in flash, in hex. */
    static std::string bootloaderHash();

    /** @return the number of boots, counted in NVS. */
    uint32_t bootCount() const { return bootCount_; }

    /** @return the name of the last reset reason, as on the BOOT line. */
    static const char* resetReasonName();

private:
    OtaBootGuard() = default;

    static constexpr uint32_t POLL_PERIOD_MS = 1000;
    static constexpr uint32_t CONTACT_POLL_PERIOD_MS = 5000;

    mutable std::mutex mutex_;
    std::unique_ptr<SelfTest> selfTest_;
    std::atomic<bool> pendingVerify_{false};
    uint32_t selfTestStartMs_ = 0;
    OtaRecord record_;
    std::atomic<bool> hadNeighbours_{false};
    uint32_t bootCount_ = 0;
    std::atomic<bool> attemptResolved_{false};

    bool loadRecord();
    void saveRecord(const OtaRecord& record);
    bool loadNeighbourFlag();
    void countBoot();
    void saveNeighbourFlag();

    void startSelfTest();
    void armRtcWatchdog(uint32_t timeoutMs);
    void extendRtcWatchdog(uint32_t timeoutMs);
    void runSelfTestTask();
    void runContactTask();
    void finishSelfTest(SelfTestVerdict verdict, FailReason reason);
    void checkOtaPath(bool nvsOk);

    void printBootLine(ImageState state);
    void reportDecision(BootAction action);

    static ShaPrefix runningShaPrefix();
};
