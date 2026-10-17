#include "cmdAuth.h"

static const uint8_t AUTH_LABEL[] = {'L', 'M', 'C', '1'};

std::vector<uint8_t> commandAuthMessage(uint16_t dst, uint32_t counter, const std::string& line) {
    std::vector<uint8_t> message(AUTH_LABEL, AUTH_LABEL + sizeof(AUTH_LABEL));
    message.push_back(static_cast<uint8_t>(dst & 0xFF));
    message.push_back(static_cast<uint8_t>(dst >> 8));
    for (int shift = 0; shift < 32; shift += 8) {
        message.push_back(static_cast<uint8_t>((counter >> shift) & 0xFF));
    }
    message.insert(message.end(), line.begin(), line.end());
    return message;
}

CommandTag commandTag(const KeyedHmac& hmac, uint16_t dst, uint32_t counter,
                      const std::string& line) {
    std::vector<uint8_t> message = commandAuthMessage(dst, counter, line);
    HmacDigest digest = hmac(message.data(), message.size());
    CommandTag tag;
    for (size_t i = 0; i < tag.size(); i++) tag[i] = digest[i];
    return tag;
}

bool commandTagsEqual(const CommandTag& a, const CommandTag& b) {
    uint8_t difference = 0;
    for (size_t i = 0; i < a.size(); i++) difference |= static_cast<uint8_t>(a[i] ^ b[i]);
    return difference == 0;
}

CounterDecision CommandCounter::decide(uint32_t counter) const {
    if (!any_ || counter > last_) return CounterDecision::EXECUTE;
    return counter == last_ ? CounterDecision::REPEAT : CounterDecision::REPLAY;
}

void CommandCounter::accept(uint32_t counter) {
    last_ = counter;
    any_ = true;
    reply_.clear();
    hasReply_ = false;
}

std::string CommandCounter::lastReply() const {
    return hasReply_ ? reply_ : "OK already executed";
}
