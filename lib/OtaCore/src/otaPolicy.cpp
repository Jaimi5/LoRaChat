#include "otaPolicy.h"

constexpr uint32_t OtaPolicy::MIN_BATTERY_MV;
constexpr uint32_t OtaPolicy::MIN_FREE_HEAP;

PolicyDecision OtaPolicy::decide(const OtaManifest& manifest, const NodeState& node) {
    if (manifest.type != ImageType::FULL_APP) return PolicyDecision::REFUSE_TYPE;
    if (manifest.boardEnv != node.boardEnv) return PolicyDecision::REFUSE_BOARD;
    if (manifest.partitionTablePrefix != node.partitionTablePrefix) {
        return PolicyDecision::REFUSE_PARTITION_TABLE;
    }
    if (manifest.imageSize > node.slotSize) return PolicyDecision::REFUSE_SIZE;
    if (node.blacklist.contains(manifest.imageShaPrefix())) {
        return PolicyDecision::SKIP_BLACKLISTED;
    }
    if (manifest.version == node.runningVersion) return PolicyDecision::NO_OP_SAME_VERSION;
    if (manifest.version < node.runningVersion && !manifest.allowDowngrade()) {
        return PolicyDecision::REFUSE_DOWNGRADE;
    }
    if (!node.externalPower && node.batteryMv < MIN_BATTERY_MV) {
        return PolicyDecision::REFUSE_BATTERY;
    }
    if (node.freeHeap < MIN_FREE_HEAP) return PolicyDecision::REFUSE_HEAP;
    return PolicyDecision::INSTALL;
}

bool OtaPolicy::descriptorMatches(const OtaManifest& manifest, const std::string& project,
                                  const std::string& version) {
    return manifest.project == project && manifest.versionString == version;
}

bool OtaPolicy::parseVersion(const std::string& text, uint32_t& out) {
    std::string core = text.substr(0, text.find('+'));
    uint32_t parts[3] = {0, 0, 0};
    size_t part = 0;
    size_t digits = 0;
    for (char c : core) {
        if (c == '.') {
            if (digits == 0 || ++part > 2) return false;
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            parts[part] = parts[part] * 10 + static_cast<uint32_t>(c - '0');
            if (++digits > 3 || parts[part] > 255) return false;
        } else {
            return false;
        }
    }
    if (part != 2 || digits == 0) return false;
    out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
    return true;
}
