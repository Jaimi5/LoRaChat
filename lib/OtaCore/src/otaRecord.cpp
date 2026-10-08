#include "otaRecord.h"

#include <algorithm>

namespace {

constexpr uint8_t MAGIC_0 = 'L';
constexpr uint8_t MAGIC_1 = 'O';
constexpr size_t HEADER_SIZE = 4;

}  // namespace

constexpr size_t Blacklist::CAPACITY;
constexpr uint8_t OtaRecordCodec::VERSION;
constexpr size_t OtaRecordCodec::V1_SIZE;

void Blacklist::add(const ShaPrefix& sha) {
    if (contains(sha)) return;
    if (count_ < CAPACITY) {
        entries_[(head_ + count_) % CAPACITY] = sha;
        count_++;
        return;
    }
    entries_[head_] = sha;
    head_ = (head_ + 1) % CAPACITY;
}

bool Blacklist::contains(const ShaPrefix& sha) const {
    for (size_t i = 0; i < count_; i++) {
        if (at(i) == sha) return true;
    }
    return false;
}

const ShaPrefix& Blacklist::at(size_t i) const {
    return entries_[(head_ + i) % CAPACITY];
}

bool Blacklist::operator==(const Blacklist& other) const {
    if (count_ != other.count_) return false;
    for (size_t i = 0; i < count_; i++) {
        if (at(i) != other.at(i)) return false;
    }
    return true;
}

bool OtaRecord::operator==(const OtaRecord& other) const {
    return attemptSha == other.attemptSha && pending == other.pending &&
           unexplainedRollbacks == other.unexplainedRollbacks &&
           failReason == other.failReason && lastOutcome == other.lastOutcome &&
           blacklist == other.blacklist;
}

std::vector<uint8_t> OtaRecordCodec::encode(const OtaRecord& record) {
    std::vector<uint8_t> out;
    out.reserve(V1_SIZE);
    out.push_back(MAGIC_0);
    out.push_back(MAGIC_1);
    out.push_back(VERSION);
    out.push_back(static_cast<uint8_t>(V1_SIZE));
    out.insert(out.end(), record.attemptSha.begin(), record.attemptSha.end());
    out.push_back(record.pending ? 1 : 0);
    out.push_back(record.unexplainedRollbacks);
    out.push_back(static_cast<uint8_t>(record.failReason));
    out.push_back(static_cast<uint8_t>(record.lastOutcome));
    out.push_back(record.blacklist.head_);
    out.push_back(record.blacklist.count_);
    for (const ShaPrefix& entry : record.blacklist.entries_) {
        out.insert(out.end(), entry.begin(), entry.end());
    }
    return out;
}

bool OtaRecordCodec::decode(const uint8_t* data, size_t size, OtaRecord& out) {
    if (data == nullptr || size < V1_SIZE) return false;
    if (data[0] != MAGIC_0 || data[1] != MAGIC_1) return false;
    size_t length = data[3];
    if (length < V1_SIZE || length > size) return false;

    const uint8_t* p = data + HEADER_SIZE;
    OtaRecord record;
    std::copy(p, p + record.attemptSha.size(), record.attemptSha.begin());
    p += record.attemptSha.size();
    record.pending = *p++ != 0;
    record.unexplainedRollbacks = *p++;
    record.failReason = static_cast<FailReason>(*p++);
    record.lastOutcome = static_cast<OtaOutcome>(*p++);

    uint8_t head = *p++;
    uint8_t count = *p++;
    if (head >= Blacklist::CAPACITY || count > Blacklist::CAPACITY) return false;
    record.blacklist.head_ = head;
    record.blacklist.count_ = count;
    for (ShaPrefix& entry : record.blacklist.entries_) {
        std::copy(p, p + entry.size(), entry.begin());
        p += entry.size();
    }

    out = record;
    return true;
}
