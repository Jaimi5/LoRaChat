#include "restartGate.h"

#include <algorithm>
#include <limits>

uint32_t restartCapMs(uint32_t superframeMs) {
    if (superframeMs == 0) return RESTART_CAP_UNKNOWN_MS;
    constexpr uint64_t MAX_MS = std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(
        std::min(2 * uint64_t{superframeMs} + RESTART_CAP_MARGIN_MS, MAX_MS));
}

bool RestartGate::due(uint32_t elapsedMs, size_t txQueued, uint32_t superframeMs) {
    if (elapsedMs < RESTART_MIN_DELAY_MS) return false;
    if (elapsedMs >= restartCapMs(superframeMs)) return true;
    if (!queueEmptied_ && txQueued == 0) {
        queueEmptied_ = true;
        emptiedAtMs_ = elapsedMs;
    }
    return queueEmptied_ && elapsedMs - emptiedAtMs_ >= RESTART_AIR_TIME_MS;
}
