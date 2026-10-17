#pragma once

#include <cstdint>
#include <string>

#include "otaManifest.h"
#include "otaRecord.h"

/** What the node has and runs, as seen by the update policy. */
struct NodeState {
    std::string boardEnv;
    /** Running image version packed as 0xMMmmpp00. */
    uint32_t runningVersion = 0;
    /** First bytes of the SHA-256 of the partition table in flash. */
    ShaPrefix partitionTablePrefix{};
    /** Size of the partition the image would be written to. */
    uint32_t slotSize = 0;
    Blacklist blacklist;
    /** Image whose download or check failed since the last power-on. */
    ShaPrefix failedImage{};
    bool hasFailedImage = false;
    uint32_t batteryMv = 0;
    /** True when the node runs on external power (USB or solar charging). */
    bool externalPower = false;
    uint32_t freeHeap = 0;
};

/** Outcome of the update policy. Values are reported, never renumber them. */
enum class PolicyDecision : uint8_t {
    INSTALL = 0,
    NO_OP_SAME_VERSION = 1,
    SKIP_BLACKLISTED = 2,
    REFUSE_TYPE = 3,
    REFUSE_BOARD = 4,
    REFUSE_PARTITION_TABLE = 5,
    REFUSE_SIZE = 6,
    REFUSE_DOWNGRADE = 7,
    REFUSE_BATTERY = 8,
    REFUSE_HEAP = 9,
    SKIP_FAILED_SINCE_POWER_ON = 10,
};

/** @return a short name of @p decision for logs and reports. */
const char* policyDecisionName(PolicyDecision decision);

/** Pure decisions about whether and how an image may be installed. */
class OtaPolicy {
public:
    static constexpr uint32_t MIN_BATTERY_MV = 3600;
    static constexpr uint32_t MIN_FREE_HEAP = 60 * 1024;

    /**
     * @brief Decides what to do with a verified manifest.
     *
     * Reasons tied to the manifest (type, board, partition table, size, blacklist, version) come
     * before the node's momentary condition (battery, heap), so a refusal for battery or heap
     * means the same manifest can be tried again later.
     */
    static PolicyDecision decide(const OtaManifest& manifest, const NodeState& node);

    /**
     * @brief Checks battery and heap, as for an install, before a maintenance window opens.
     * @return INSTALL if they are enough, else REFUSE_BATTERY or REFUSE_HEAP.
     */
    static PolicyDecision checkResources(uint32_t batteryMv, bool externalPower,
                                         uint32_t freeHeap);

    /**
     * @brief Checks the descriptor of the written image against the manifest.
     * @param project esp_app_desc_t.project_name of the written image.
     * @param version esp_app_desc_t.version of the written image.
     */
    static bool descriptorMatches(const OtaManifest& manifest, const std::string& project,
                                  const std::string& version);

    /**
     * @brief Parses MAJOR.MINOR.PATCH (each 0-255, optional +build metadata) as 0xMMmmpp00.
     * @return false if @p text is not such a version.
     */
    static bool parseVersion(const std::string& text, uint32_t& out);
};
