#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

/** First bytes of the SHA-256 of an image, used to identify it in NVS. */
using ShaPrefix = std::array<uint8_t, 8>;

/** Why a new image asked for a rollback. Values are persisted, never renumber them. */
enum class FailReason : uint8_t {
    NONE = 0,
    RADIO = 1,
    MESH_SILENT = 2,
    HEAP = 3,
    PMU = 4,
    OTA_PATH = 5,
    DEADLINE = 6,
};

/** Result of the last update attempt, as reported by `/ota.status`. Persisted values. */
enum class OtaOutcome : uint8_t {
    NONE = 0,
    VALID = 1,
    ROLLED_BACK = 2,
    BL_NO_ROLLBACK = 3,
};

/**
 * @brief Ring of image SHA prefixes that must never be installed again.
 *
 * Holds up to CAPACITY entries. When full, a new entry replaces the oldest one.
 */
class Blacklist {
public:
    static constexpr size_t CAPACITY = 4;

    /** Adds @p sha unless it is already present. */
    void add(const ShaPrefix& sha);

    /** @return true if @p sha is in the ring. */
    bool contains(const ShaPrefix& sha) const;

    /** @return number of entries in use. */
    size_t size() const { return count_; }

    /** @return entry @p i in insertion order, 0 being the oldest. */
    const ShaPrefix& at(size_t i) const;

    bool operator==(const Blacklist& other) const;

private:
    friend class OtaRecordCodec;

    std::array<ShaPrefix, CAPACITY> entries_{};
    uint8_t head_ = 0;
    uint8_t count_ = 0;
};

/**
 * @brief Persistent OTA state shared by every image on the node.
 *
 * The downloader marks an attempt as pending before rebooting into the new image. The boot
 * guard of whichever image runs next resolves it.
 */
struct OtaRecord {
    ShaPrefix attemptSha{};
    bool pending = false;
    /** Rollbacks of attemptSha with no recorded reason (crash, watchdog, power cut). */
    uint8_t unexplainedRollbacks = 0;
    /** Reason written by the new image right before it asks for a rollback. */
    FailReason failReason = FailReason::NONE;
    OtaOutcome lastOutcome = OtaOutcome::NONE;
    Blacklist blacklist;

    bool operator==(const OtaRecord& other) const;
};

/**
 * @brief Versioned binary encoding of OtaRecord for a single NVS blob.
 *
 * Layout: magic "LO", version, total length, then the fields. New fields are only appended,
 * so an older image decodes a record written by a newer one and ignores the tail.
 */
class OtaRecordCodec {
public:
    static constexpr uint8_t VERSION = 1;
    static constexpr size_t V1_SIZE = 4 + 8 + 4 + 2 + Blacklist::CAPACITY * 8;

    static std::vector<uint8_t> encode(const OtaRecord& record);

    /**
     * @brief Decodes @p size bytes at @p data into @p out.
     * @return false if the blob is not a record (bad magic, too short, inconsistent length).
     */
    static bool decode(const uint8_t* data, size_t size, OtaRecord& out);
};
