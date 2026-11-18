#include "installMode.h"

#include <algorithm>
#include <limits>

uint32_t InstallMode::start(uint32_t seconds, uint64_t nowMs) {
    uint32_t accepted = std::min(seconds, INSTALL_MAX_SECONDS);
    untilMs_ = accepted == 0 ? 0 : nowMs + static_cast<uint64_t>(accepted) * 1000;
    return accepted;
}

uint64_t InstallMode::remainingMs(uint64_t nowMs) const {
    uint64_t untilMs = untilMs_;
    return nowMs < untilMs ? untilMs - nowMs : 0;
}

uint32_t InstallMode::remainingS(uint64_t nowMs) const {
    return static_cast<uint32_t>((remainingMs(nowMs) + 999) / 1000);
}

uint32_t InstallMode::reportIntervalMs(uint32_t superframeMs) {
    uint32_t limit = std::numeric_limits<uint32_t>::max() / INSTALL_REPORT_SUPERFRAMES;
    uint32_t superframes = std::min(superframeMs, limit) * INSTALL_REPORT_SUPERFRAMES;
    return std::max(INSTALL_MIN_REPORT_MS, superframes);
}

bool parseInstallSeconds(const std::string& args, uint32_t& seconds) {
    if (args.empty()) {
        seconds = INSTALL_DEFAULT_SECONDS;
        return true;
    }
    if (args.size() > 4) return false;
    uint32_t value = 0;
    for (char c : args) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint32_t>(c - '0');
    }
    if (value > INSTALL_MAX_SECONDS) return false;
    seconds = value;
    return true;
}
