#include "maintWindowPlan.h"

constexpr size_t MaintWindowPlan::MAX_PER_DAY;
constexpr uint32_t MaintWindowPlan::DAY_S;
constexpr uint32_t MaintWindowPlan::MAX_PULL_S;
constexpr uint32_t MaintWindowPlan::MAX_AP_S;
constexpr uint32_t MaintWindowPlan::RESULT_GRACE_S;
constexpr uint32_t MaintWindowPlan::SAVE_IN_WINDOW_S;
constexpr uint32_t MaintWindowPlan::SAVE_WHILE_COUNTING_S;

namespace {

const uint8_t MAGIC[] = {'L', 'W'};
constexpr uint8_t FORMAT_VERSION = 1;
constexpr size_t V1_SIZE = 2 + 1 + 1 + 1 + 4 + 1 + 4 * MaintWindowPlan::MAX_PER_DAY;

/** @return true if node time @p a is later than @p b, also across a wrap of the clock. */
bool later(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) > 0;
}

void putU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>(value >> shift));
}

uint32_t getU32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 |
           static_cast<uint32_t>(data[2]) << 16 | static_cast<uint32_t>(data[3]) << 24;
}

}  // namespace

OpenDecision MaintWindowPlan::open(WindowKind kind, uint32_t durationS, uint32_t now) {
    uint32_t maxS = kind == WindowKind::PULL ? MAX_PULL_S : kind == WindowKind::AP ? MAX_AP_S : 0;
    if (durationS == 0 || durationS > maxS) return OpenDecision::REFUSED_DURATION;
    if (active(now) == kind) {
        if (later(now + durationS, endsAt_)) endsAt_ = now + durationS;
        return OpenDecision::EXTENDED;
    }
    if (countedInLastDay(now) >= MAX_PER_DAY) return OpenDecision::REFUSED_LIMIT;
    recordOpen(now);
    kind_ = kind;
    endsAt_ = now + durationS;
    return OpenDecision::OPENED;
}

void MaintWindowPlan::onResult(uint32_t now) {
    if (active(now) != WindowKind::AP) return;
    if (later(endsAt_, now + RESULT_GRACE_S)) endsAt_ = now + RESULT_GRACE_S;
}

WindowKind MaintWindowPlan::active(uint32_t now) const {
    return kind_ != WindowKind::NONE && later(endsAt_, now) ? kind_ : WindowKind::NONE;
}

uint32_t MaintWindowPlan::remaining(uint32_t now) const {
    return active(now) == WindowKind::NONE ? 0 : endsAt_ - now;
}

size_t MaintWindowPlan::countedInLastDay(uint32_t now) const {
    size_t count = 0;
    for (size_t i = 0; i < historyCount_; i++) {
        if (now - history_[i] < DAY_S) count++;
    }
    return count;
}

bool MaintWindowPlan::clockSaveDue(uint32_t now, uint32_t savedAt) const {
    uint32_t elapsed = now - savedAt;
    if (active(now) != WindowKind::NONE) return elapsed >= SAVE_IN_WINDOW_S;
    if (countedInLastDay(now) > 0) return elapsed >= SAVE_WHILE_COUNTING_S;
    return false;
}

void MaintWindowPlan::recordOpen(uint32_t now) {
    // Entries older than a day no longer count; when the ring is full the oldest goes.
    size_t kept = 0;
    for (size_t i = 0; i < historyCount_; i++) {
        if (now - history_[i] < DAY_S) history_[kept++] = history_[i];
    }
    if (kept == history_.size()) {
        for (size_t i = 1; i < kept; i++) history_[i - 1] = history_[i];
        kept--;
    }
    history_[kept++] = now;
    historyCount_ = static_cast<uint8_t>(kept);
}

std::vector<uint8_t> MaintWindowPlan::encode() const {
    std::vector<uint8_t> out(MAGIC, MAGIC + sizeof(MAGIC));
    out.push_back(FORMAT_VERSION);
    out.push_back(static_cast<uint8_t>(V1_SIZE));
    out.push_back(static_cast<uint8_t>(kind_));
    putU32(out, endsAt_);
    out.push_back(historyCount_);
    for (uint32_t entry : history_) putU32(out, entry);
    return out;
}

bool MaintWindowPlan::decode(const uint8_t* data, size_t size, MaintWindowPlan& out) {
    if (data == nullptr || size < V1_SIZE || data[0] != MAGIC[0] || data[1] != MAGIC[1] ||
        data[2] < FORMAT_VERSION || data[3] != size) {
        return false;
    }
    uint8_t kind = data[4];
    uint8_t count = data[9];
    if (kind > static_cast<uint8_t>(WindowKind::AP) || count > MAX_PER_DAY) return false;

    MaintWindowPlan plan;
    plan.kind_ = static_cast<WindowKind>(kind);
    plan.endsAt_ = getU32(data + 5);
    plan.historyCount_ = count;
    for (size_t i = 0; i < MAX_PER_DAY; i++) plan.history_[i] = getU32(data + 10 + 4 * i);
    out = plan;
    return true;
}

bool MaintWindowPlan::operator==(const MaintWindowPlan& other) const {
    if (kind_ != other.kind_ || endsAt_ != other.endsAt_ || historyCount_ != other.historyCount_) {
        return false;
    }
    for (size_t i = 0; i < historyCount_; i++) {
        if (history_[i] != other.history_[i]) return false;
    }
    return true;
}
