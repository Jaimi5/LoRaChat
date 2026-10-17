#pragma once

#include <algorithm>
#include <cstring>
#include <vector>

#include "iOtaFlash.h"
#include "mbedtls/sha256.h"

/**
 * @brief Update slot with NOR flash semantics and the checks of esp_ota_*.
 *
 * Erasing sets a 4 KB sector to 0xFF and programming can only clear bits, like the real chip.
 * begin() erases nothing; write() erases each sector the first time it reaches it, as
 * esp_ota_begin(OTA_WITH_SEQUENTIAL_WRITES) does. end() verifies the appended digest like
 * esp_image_verify(). The slot starts filled with an older image (0x00 bytes), so a missing
 * erase shows up as a wrong read-back.
 */
class FakeFlash : public IOtaFlash {
public:
    static constexpr size_t SECTOR_SIZE = 4096;
    static constexpr size_t DIGEST_SIZE = 32;
    /** Offsets of esp_app_desc_t fields: image header 24 B, segment header 8 B. */
    static constexpr size_t DESC_OFFSET = 32;
    static constexpr size_t DESC_VERSION_OFFSET = DESC_OFFSET + 16;
    static constexpr size_t DESC_PROJECT_OFFSET = DESC_OFFSET + 48;
    static constexpr size_t DESC_FIELD_SIZE = 32;

    explicit FakeFlash(size_t slotSize) : slot_(slotSize, 0x00) {}

    bool begin(uint32_t size) override {
        beginCalls++;
        if (writing_ || size > slot_.size()) return false;
        writing_ = true;
        ended_ = false;
        expected_ = size;
        position_ = 0;
        erasedUntil_ = 0;
        return true;
    }

    bool write(const uint8_t* data, size_t size) override {
        if (!writing_ || position_ + size > slot_.size()) return false;
        for (size_t i = 0; i < size; i++) {
            if (failWriteAt >= 0 && position_ == static_cast<size_t>(failWriteAt)) return false;
            if (position_ >= erasedUntil_) {
                size_t sectorEnd = std::min(erasedUntil_ + SECTOR_SIZE, slot_.size());
                std::fill(slot_.begin() + erasedUntil_, slot_.begin() + sectorEnd, 0xFF);
                erasedUntil_ = sectorEnd;
            }
            slot_[position_++] &= data[i];
        }
        return true;
    }

    bool end() override {
        if (!writing_) return false;
        writing_ = false;
        if (failEnd || position_ != expected_ || expected_ <= DIGEST_SIZE) return false;
        Sha256 digest = hash(expected_ - DIGEST_SIZE);
        if (!std::equal(digest.begin(), digest.end(), slot_.begin() + expected_ - DIGEST_SIZE)) {
            return false;
        }
        ended_ = true;
        return true;
    }

    void abort() override {
        abortCalls++;
        writing_ = false;
    }

    bool writtenSha(Sha256& out) override {
        if (!ended_ || failReadback) return false;
        out = hash(expected_ - DIGEST_SIZE);
        return true;
    }

    bool readDescriptor(ImageDescriptor& out) override {
        if (!ended_) return false;
        out.version = field(DESC_VERSION_OFFSET);
        out.project = field(DESC_PROJECT_OFFSET);
        return true;
    }

    bool activate() override {
        if (!ended_) return false;
        bootSlotChanged = true;
        return true;
    }

    /** Bytes [0, size) of the slot. */
    std::vector<uint8_t> contents(size_t size) const {
        return std::vector<uint8_t>(slot_.begin(), slot_.begin() + size);
    }

    bool isWriting() const { return writing_; }

    /** Position at which the next write fails, or -1. */
    long failWriteAt = -1;
    bool failEnd = false;
    bool failReadback = false;
    bool bootSlotChanged = false;
    int beginCalls = 0;
    int abortCalls = 0;

private:
    Sha256 hash(size_t size) const {
        Sha256 out{};
        mbedtls_sha256_ret(slot_.data(), size, out.data(), 0);
        return out;
    }

    std::string field(size_t offset) const {
        const char* text = reinterpret_cast<const char*>(slot_.data() + offset);
        return std::string(text, strnlen(text, DESC_FIELD_SIZE));
    }

    std::vector<uint8_t> slot_;
    size_t expected_ = 0;
    size_t position_ = 0;
    size_t erasedUntil_ = 0;
    bool writing_ = false;
    bool ended_ = false;
};
