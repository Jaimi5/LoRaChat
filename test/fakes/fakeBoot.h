#pragma once

#include <array>
#include <cstdint>

#include "bootGuardLogic.h"

/**
 * @brief Two-slot model of the IDF 4.4 bootloader with app rollback.
 *
 * set_boot_partition writes NEW. The bootloader turns NEW into PENDING_VERIFY and boots it.
 * Booting a PENDING_VERIFY slot again means the app never confirmed it, so the bootloader marks
 * it ABORTED and boots the other slot. A bootloader built without rollback leaves NEW alone.
 */
class FakeBoot {
public:
    FakeBoot(const ShaPrefix& slot0, const ShaPrefix& slot1, bool bootloaderRollback = true)
        : images_{slot0, slot1}, rollback_(bootloaderRollback) {
        states_[0] = ImageState::VALID;
        states_[1] = ImageState::UNDEFINED;
    }

    /** Flashes @p sha into the slot that is not running and selects it. */
    void installAndSelect(const ShaPrefix& sha) {
        uint8_t other = running_ ^ 1;
        images_[other] = sha;
        boot_ = other;
        states_[other] = ImageState::NEW;
    }

    /** Any reset: crash, watchdog, power cut or esp_restart. */
    void reboot() {
        if (rollback_ && states_[boot_] == ImageState::PENDING_VERIFY) {
            states_[boot_] = ImageState::ABORTED;
            boot_ ^= 1;
        }
        if (rollback_ && states_[boot_] == ImageState::NEW) {
            states_[boot_] = ImageState::PENDING_VERIFY;
        }
        running_ = boot_;
    }

    void markValid() {
        states_[running_] = ImageState::VALID;
    }

    void markInvalidAndReboot() {
        states_[running_] = ImageState::INVALID;
        boot_ = running_ ^ 1;
        reboot();
    }

    ImageState runningState() const { return states_[running_]; }
    const ShaPrefix& runningSha() const { return images_[running_]; }
    uint8_t runningSlot() const { return running_; }

private:
    std::array<ShaPrefix, 2> images_;
    std::array<ImageState, 2> states_{};
    uint8_t running_ = 0;
    uint8_t boot_ = 0;
    bool rollback_;
};
