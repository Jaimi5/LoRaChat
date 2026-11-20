#include "cmdPending.h"

#include <algorithm>
#include <limits>

namespace {

constexpr uint64_t MAX_MS = std::numeric_limits<uint32_t>::max();

uint32_t saturate(uint64_t ms) {
    return static_cast<uint32_t>(std::min(ms, MAX_MS));
}

uint32_t leftUntil(uint32_t atMs, uint32_t waitedMs) {
    return atMs > waitedMs ? atMs - waitedMs : 0;
}

std::string number(uint32_t value) {
    return std::to_string(static_cast<unsigned long>(value));
}

}  // namespace

uint32_t queuedSlotWaitMs(uint32_t untilSlotMs, size_t queued, const TxSlots& slots,
                          uint32_t toleranceMs) {
    const uint64_t superframe = slots.superframeMs;
    const size_t count = slots.startsMs.size();
    if (count == 0 || superframe == 0) return saturate(untilSlotMs + queued * superframe);

    std::vector<uint64_t> starts;
    starts.reserve(count);
    for (uint32_t start : slots.startsMs) starts.push_back(start % superframe);
    std::sort(starts.begin(), starts.end());
    // Time from slot `from` to slot `to` later in the cycle. The same place is a superframe away.
    auto ahead = [&starts, superframe, count](size_t from, size_t to) {
        uint64_t distance = (starts[to % count] + superframe - starts[from % count]) % superframe;
        return distance == 0 ? superframe : distance;
    };

    uint64_t wait = untilSlotMs + queued / count * superframe;
    const size_t skipped = queued % count;
    if (skipped == 0) return saturate(wait);

    // The messages queued first take the next slots. Every slot that can be the next one gives
    // a wait for the slot left to this message, and the longest counts.
    uint64_t longestFitting = 0;
    uint64_t longest = 0;
    for (size_t next = 0; next < count; next++) {
        uint64_t extra = 0;
        for (size_t step = 0; step < skipped; step++) extra += ahead(next + step, next + step + 1);
        longest = std::max(longest, extra);
        bool fits = untilSlotMs <= ahead(next + count - 1, next) + toleranceMs;
        if (fits) longestFitting = std::max(longestFitting, extra);
    }
    wait += longestFitting > 0 ? longestFitting : longest;
    return saturate(wait);
}

uint32_t attemptTimeMs(const AttemptTiming& timing) {
    const uint64_t superframe = timing.superframeMs;
    uint64_t total =
        uint64_t{timing.slotWaitMs} + (uint64_t{timing.hops} + 1) * superframe + COMMAND_GUARD_MS;
    if (superframe == 0) {
        total += timing.allowanceMs;
    } else {
        total += (uint64_t{timing.allowanceMs} + superframe - 1) / superframe * superframe;
    }
    return saturate(total);
}

uint32_t expectedReplyMs(const AttemptTiming& timing, bool measured, uint32_t srttMs) {
    if (measured) return saturate(uint64_t{timing.slotWaitMs} + srttMs + COMMAND_GUARD_MS);
    return saturate(uint64_t{timing.slotWaitMs} + uint64_t{timing.hops} * timing.superframeMs);
}

uint32_t ceilSeconds(uint32_t ms) {
    return ms / 1000 + (ms % 1000 != 0 ? 1 : 0);
}

uint32_t PendingCommand::remainingMs(uint32_t nowMs) const {
    return leftUntil(giveUpMs, waitedMs(nowMs));
}

uint32_t PendingCommand::expectedInMs(uint32_t nowMs) const {
    return leftUntil(expectedMs, waitedMs(nowMs));
}

std::string formatQueuedAck(const PendingCommand& command, uint32_t nowMs) {
    return number(command.originRequestId) + " Q " +
           number(ceilSeconds(command.remainingMs(nowMs))) + " " +
           number(ceilSeconds(command.expectedInMs(nowMs))) + " " + number(command.timing.hops) +
           " " + number(command.timing.superframeMs);
}

std::string formatRetransmitAck(const PendingCommand& command, uint32_t nowMs) {
    return number(command.originRequestId) + " R " +
           number(ceilSeconds(command.remainingMs(nowMs)));
}

std::string formatDoneAck(const PendingCommand& command, uint32_t nowMs) {
    return number(command.originRequestId) + " D " + number(command.waitedMs(nowMs)) + " " +
           number(command.attempts());
}

PendingCommands::PendingCommands(size_t slots, size_t lateSlots)
    : slots_(std::max<size_t>(slots, 1)), late_(std::max<size_t>(lateSlots, 1)) {}

uint8_t PendingCommands::nextRequestId(uint32_t nowMs) {
    forgetStale(nowMs);
    auto holds = [](const std::vector<Slot>& slots, uint8_t id) {
        return std::any_of(slots.begin(), slots.end(), [id](const Slot& slot) {
            return slot.used && slot.command.requestId == id;
        });
    };
    // Both tables together have fewer slots than there are ids, so a free id always exists.
    for (size_t attempt = 0; attempt <= std::numeric_limits<uint8_t>::max(); attempt++) {
        lastRequestId_ = static_cast<uint8_t>(lastRequestId_ + 1);
        if (!holds(slots_, lastRequestId_) && !holds(late_, lastRequestId_)) break;
    }
    return lastRequestId_;
}

bool PendingCommands::add(PendingCommand& command, uint32_t nowMs, PendingCommand& evicted) {
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
    command.sentMs = nowMs;
    command.retransmitted = false;
    command.attemptMs = attemptTimeMs(command.timing);
    command.giveUpMs = command.attemptMs;
    if (command.isSigned) {
        AttemptTiming second = command.timing;
        second.slotWaitMs = second.superframeMs;
        command.giveUpMs = saturate(uint64_t{command.attemptMs} + attemptTimeMs(second));
    }
    target->used = true;
    target->command = command;
    return evict;
}

const PendingCommand* PendingCommands::findRepeat(Origin origin, uint16_t dst,
                                                  uint8_t originRequestId,
                                                  const std::vector<uint8_t>& frame) const {
    for (const Slot& slot : slots_) {
        const PendingCommand& command = slot.command;
        if (slot.used && command.origin == origin && command.dst == dst &&
            command.originRequestId == originRequestId && command.frame == frame) {
            return &command;
        }
    }
    return nullptr;
}

PendingCommands::Slot* PendingCommands::waiting(uint16_t dst, uint8_t requestId) {
    for (Slot& slot : slots_) {
        if (slot.used && slot.command.dst == dst && slot.command.requestId == requestId) {
            return &slot;
        }
    }
    return nullptr;
}

bool PendingCommands::take(uint16_t src, uint8_t requestId, PendingCommand& out) {
    Slot* slot = waiting(src, requestId);
    if (slot == nullptr) return false;
    out = slot->command;
    *slot = Slot{};
    return true;
}

bool PendingCommands::takeLate(uint16_t src, uint8_t requestId, uint32_t nowMs,
                               PendingCommand& out) {
    forgetStale(nowMs);
    for (Slot& slot : late_) {
        if (!slot.used || slot.command.dst != src || slot.command.requestId != requestId) continue;
        out = slot.command;
        slot = Slot{};
        return true;
    }
    return false;
}

void PendingCommands::sweep(uint32_t nowMs, std::vector<PendingCommand>& retransmit,
                            std::vector<PendingCommand>& expired) {
    forgetStale(nowMs);
    for (Slot& slot : slots_) {
        if (!slot.used) continue;
        PendingCommand& command = slot.command;
        uint32_t waitedMs = command.waitedMs(nowMs);
        if (waitedMs >= command.giveUpMs) {
            expired.push_back(command);
            remember(command, nowMs);
            slot = Slot{};
        } else if (command.isSigned && !command.retransmitted && waitedMs >= command.attemptMs) {
            command.retransmitted = true;
            retransmit.push_back(command);
        }
    }
}

bool PendingCommands::secondAttempt(uint16_t dst, uint8_t requestId, uint32_t nowMs,
                                    const AttemptTiming& timing, uint32_t expectedInMs,
                                    PendingCommand& out) {
    Slot* slot = waiting(dst, requestId);
    if (slot == nullptr) return false;
    PendingCommand& command = slot->command;
    uint64_t waitedMs = command.waitedMs(nowMs);
    command.retransmitted = true;
    command.giveUpMs = saturate(waitedMs + attemptTimeMs(timing));
    command.expectedMs = saturate(waitedMs + expectedInMs);
    out = command;
    return true;
}

bool PendingCommands::giveUp(uint16_t dst, uint8_t requestId, uint32_t nowMs, PendingCommand& out) {
    Slot* slot = waiting(dst, requestId);
    if (slot == nullptr) return false;
    out = slot->command;
    remember(slot->command, nowMs);
    *slot = Slot{};
    return true;
}

bool PendingCommands::cancel(Origin origin, uint16_t dst, uint8_t originRequestId, uint32_t nowMs,
                             PendingCommand& out) {
    bool found = false;
    uint32_t oldestAgeMs = 0;
    for (Slot& slot : slots_) {
        const PendingCommand& command = slot.command;
        if (!slot.used || command.origin != origin || command.dst != dst ||
            command.originRequestId != originRequestId) {
            continue;
        }
        // Ages are compared, since the clock may have wrapped between two commands.
        uint32_t ageMs = command.waitedMs(nowMs);
        if (!found || ageMs > oldestAgeMs) {
            out = command;
            oldestAgeMs = ageMs;
        }
        found = true;
        remember(command, nowMs);
        slot = Slot{};
    }
    return found;
}

size_t PendingCommands::size() const {
    return static_cast<size_t>(
        std::count_if(slots_.begin(), slots_.end(), [](const Slot& slot) { return slot.used; }));
}

size_t PendingCommands::lateSize(uint32_t nowMs) const {
    return static_cast<size_t>(std::count_if(late_.begin(), late_.end(), [nowMs](const Slot& slot) {
        return slot.used && nowMs - slot.expiredMs <= COMMAND_LATE_REPLY_MS;
    }));
}

void PendingCommands::remember(const PendingCommand& command, uint32_t nowMs) {
    auto target =
        std::find_if(late_.begin(), late_.end(), [](const Slot& slot) { return !slot.used; });
    if (target == late_.end()) {
        target =
            std::max_element(late_.begin(), late_.end(), [nowMs](const Slot& a, const Slot& b) {
                return nowMs - a.expiredMs < nowMs - b.expiredMs;
            });
    }
    target->used = true;
    target->expiredMs = nowMs;
    target->command = command;
    target->command.frame.clear();
    target->command.frame.shrink_to_fit();
}

void PendingCommands::forgetStale(uint32_t nowMs) {
    for (Slot& slot : late_) {
        if (slot.used && nowMs - slot.expiredMs > COMMAND_LATE_REPLY_MS) slot = Slot{};
    }
}
