#include "cmdPending.h"

#include <algorithm>
#include <limits>

PendingCommands::PendingCommands(size_t slots) : slots_(std::max<size_t>(slots, 1)) {}

uint32_t PendingCommands::timeoutMs(uint32_t superframeMs) {
    uint32_t limit = std::numeric_limits<uint32_t>::max() / COMMAND_TIMEOUT_SUPERFRAMES;
    uint32_t superframes = std::min(superframeMs, limit) * COMMAND_TIMEOUT_SUPERFRAMES;
    return std::max(COMMAND_MIN_TIMEOUT_MS, superframes);
}

uint8_t PendingCommands::nextRequestId() {
    // The table has fewer slots than there are ids, so a free id always exists.
    for (size_t attempt = 0; attempt <= std::numeric_limits<uint8_t>::max(); attempt++) {
        lastRequestId_ = static_cast<uint8_t>(lastRequestId_ + 1);
        uint8_t candidate = lastRequestId_;
        bool taken = std::any_of(slots_.begin(), slots_.end(), [candidate](const Slot& slot) {
            return slot.used && slot.command.requestId == candidate;
        });
        if (!taken) break;
    }
    return lastRequestId_;
}

bool PendingCommands::add(const PendingCommand& command, uint32_t nowMs, PendingCommand& evicted) {
    auto target =
        std::find_if(slots_.begin(), slots_.end(), [](const Slot& slot) { return !slot.used; });
    bool evict = target == slots_.end();
    if (evict) {
        // Ages are compared, since the clock may have wrapped between two commands.
        target =
            std::max_element(slots_.begin(), slots_.end(), [nowMs](const Slot& a, const Slot& b) {
                return nowMs - a.command.sentMs < nowMs - b.command.sentMs;
            });
        if (target == slots_.end()) return false;
        evicted = target->command;
    }
    target->used = true;
    target->command = command;
    target->command.sentMs = nowMs;
    target->command.retransmitted = false;
    return evict;
}

bool PendingCommands::take(uint16_t src, uint8_t requestId, PendingCommand& out) {
    for (Slot& slot : slots_) {
        if (!slot.used || slot.command.dst != src || slot.command.requestId != requestId) continue;
        out = slot.command;
        slot = Slot{};
        return true;
    }
    return false;
}

void PendingCommands::sweep(uint32_t nowMs, std::vector<PendingCommand>& retransmit,
                            std::vector<PendingCommand>& expired) {
    for (Slot& slot : slots_) {
        if (!slot.used) continue;
        uint32_t waitedMs = nowMs - slot.command.sentMs;
        if (waitedMs >= slot.command.timeoutMs) {
            expired.push_back(slot.command);
            slot = Slot{};
        } else if (slot.command.isSigned && !slot.command.retransmitted &&
                   waitedMs >= slot.command.timeoutMs / 2) {
            slot.command.retransmitted = true;
            retransmit.push_back(slot.command);
        }
    }
}

size_t PendingCommands::size() const {
    return static_cast<size_t>(
        std::count_if(slots_.begin(), slots_.end(), [](const Slot& slot) { return slot.used; }));
}
