#include "cmdRtt.h"

#include <algorithm>
#include <limits>

namespace {

uint32_t saturate(uint64_t value) {
    return static_cast<uint32_t>(std::min<uint64_t>(value, std::numeric_limits<uint32_t>::max()));
}

}  // namespace

RttEstimator::RttEstimator(size_t entries) : entries_(std::max<size_t>(entries, 1)) {}

RttEstimator::Entry* RttEstimator::find(uint16_t dst) {
    for (Entry& entry : entries_) {
        if (entry.used && entry.path.dst == dst) return &entry;
    }
    return nullptr;
}

bool RttEstimator::follow(Entry& entry, const RttPath& path) {
    if (entry.path.hops != path.hops || entry.path.nextHop != path.nextHop) {
        entry = Entry{};
        return false;
    }
    if (entry.path.superframeMs != path.superframeMs) {
        if (entry.path.superframeMs == 0) {
            entry = Entry{};
            return false;
        }
        entry.srttMs =
            saturate(uint64_t{entry.srttMs} * path.superframeMs / entry.path.superframeMs);
        entry.rttvarMs = saturate(uint64_t{entry.rttvarMs} * 2);
        entry.path.superframeMs = path.superframeMs;
    }
    return true;
}

bool RttEstimator::addSample(const RttPath& path, uint32_t elapsedMs, uint32_t slotWaitMs,
                             bool retransmitted) {
    if (retransmitted) return false;
    const uint32_t sample = elapsedMs > slotWaitMs ? elapsedMs - slotWaitMs : 0;
    Entry* entry = find(path.dst);
    if (entry != nullptr && follow(*entry, path)) {
        const uint32_t difference =
            entry->srttMs > sample ? entry->srttMs - sample : sample - entry->srttMs;
        entry->rttvarMs = saturate((uint64_t{entry->rttvarMs} * 3 + difference) / 4);
        entry->srttMs = saturate((uint64_t{entry->srttMs} * 7 + sample) / 8);
    } else {
        if (entry == nullptr) {
            entry = &*std::min_element(entries_.begin(), entries_.end(),
                                       [](const Entry& a, const Entry& b) {
                                           if (a.used != b.used) return !a.used;
                                           return a.updated < b.updated;
                                       });
        }
        entry->used = true;
        entry->path = path;
        entry->srttMs = sample;
        entry->rttvarMs = sample / 2;
    }
    entry->updated = ++updates_;
    return true;
}

bool RttEstimator::estimate(const RttPath& path, uint32_t& srttMs) {
    Entry* entry = find(path.dst);
    if (entry == nullptr || !follow(*entry, path)) return false;
    srttMs = entry->srttMs;
    return true;
}

uint32_t RttEstimator::variationMs(uint16_t dst) const {
    for (const Entry& entry : entries_) {
        if (entry.used && entry.path.dst == dst) return entry.rttvarMs;
    }
    return 0;
}

size_t RttEstimator::size() const {
    return static_cast<size_t>(std::count_if(entries_.begin(), entries_.end(),
                                             [](const Entry& entry) { return entry.used; }));
}
