#include "cmdAuth.h"

static const uint8_t AUTH_LABEL[] = {'L', 'M', 'C', '1'};

const char* const COMMAND_ALREADY_EXECUTED_TEXT = "OK already executed";

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
    reply_ = CommandResult();
    hasReply_ = false;
    binary_ = BinaryReply();
    hasBinary_ = false;
}

CommandResult CommandCounter::lastReply() const {
    if (hasReply_) return reply_;
    if (hasBinary_ && binary_.status != 0) return CommandResult(binary_.status, "");
    return CommandResult(COMMAND_ALREADY_EXECUTED_TEXT);
}

BinaryReply CommandCounter::lastBinaryReply() const {
    if (hasBinary_) return binary_;
    BinaryReply reply;
    reply.status = hasReply_ ? reply_.status : 0;
    reply.replayed = reply.status == 0;
    return reply;
}

CommandResult runOncePerCounter(CommandCounter& counter, uint32_t value,
                                const std::function<bool(uint32_t)>& store,
                                const std::function<CommandResult()>& execute) {
    switch (counter.decide(value)) {
        case CounterDecision::REPLAY:
            return CommandStatus::REPLAY;
        case CounterDecision::REPEAT:
            return counter.lastReply();
        case CounterDecision::EXECUTE:
            break;
    }
    // The counter is stored before the command runs, so it never runs twice.
    if (!store(value)) return CommandStatus::STORAGE;
    counter.accept(value);
    CommandResult reply = execute();
    counter.setReply(reply);
    return reply;
}

BinaryReply runBinaryOncePerCounter(CommandCounter& counter, uint32_t value,
                                    const std::function<bool(uint32_t)>& store,
                                    const std::function<BinaryReply()>& execute) {
    BinaryReply refused;
    switch (counter.decide(value)) {
        case CounterDecision::REPLAY:
            refused.status = static_cast<uint8_t>(CommandStatus::REPLAY);
            return refused;
        case CounterDecision::REPEAT:
            return counter.lastBinaryReply();
        case CounterDecision::EXECUTE:
            break;
    }
    if (!store(value)) {
        refused.status = static_cast<uint8_t>(CommandStatus::STORAGE);
        return refused;
    }
    counter.accept(value);
    BinaryReply reply = execute();
    counter.setBinaryReply(reply);
    return reply;
}
