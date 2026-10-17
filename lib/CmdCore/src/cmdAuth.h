#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/** Truncated HMAC-SHA256 that signs a command. */
using CommandTag = std::array<uint8_t, 8>;
using HmacDigest = std::array<uint8_t, 32>;
/** HMAC-SHA256 under the deployment key, provided by the crypto library or a test double. */
using KeyedHmac = std::function<HmacDigest(const uint8_t* data, size_t size)>;

/** @return the bytes the tag covers: "LMC1" || dst u16 LE || counter u32 LE || line. */
std::vector<uint8_t> commandAuthMessage(uint16_t dst, uint32_t counter, const std::string& line);

/** @return the first 8 bytes of the HMAC over commandAuthMessage(). */
CommandTag commandTag(const KeyedHmac& hmac, uint16_t dst, uint32_t counter,
                      const std::string& line);

/** Compares two tags in constant time. */
bool commandTagsEqual(const CommandTag& a, const CommandTag& b);

/** What to do with a signed command, judged by its counter. */
enum class CounterDecision : uint8_t {
    /** Newer than any seen: store the counter, then run the command. */
    EXECUTE,
    /** The last command again (its reply was lost): answer with the stored reply. */
    REPEAT,
    /** Older than the last command: refuse. */
    REPLAY,
};

/**
 * @brief Replay protection for signed commands.
 *
 * Counters only grow. The node stores the last counter in NVS before running a command, so a
 * command never runs twice, even across a reboot. The reply of the last command is kept in RAM
 * for repeats.
 */
class CommandCounter {
public:
    explicit CommandCounter(uint32_t last = 0, bool any = false) : last_(last), any_(any) {}

    CounterDecision decide(uint32_t counter) const;

    /** Records @p counter as the last one; its reply is not known yet. */
    void accept(uint32_t counter);

    /** Stores the reply of the last accepted command. */
    void setReply(const std::string& reply) {
        reply_ = reply;
        hasReply_ = true;
    }

    /** @return the reply of the last command, or a fixed text if it was lost in a reboot. */
    std::string lastReply() const;

    uint32_t last() const { return last_; }

private:
    uint32_t last_;
    bool any_;
    std::string reply_;
    bool hasReply_ = false;
};
