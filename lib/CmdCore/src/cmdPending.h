#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cmdPerm.h"

/** Shortest time a forwarded command waits for its reply. */
constexpr uint32_t COMMAND_MIN_TIMEOUT_MS = 30000;
/** Superframes a forwarded command waits for its reply. */
constexpr uint32_t COMMAND_TIMEOUT_SUPERFRAMES = 3;

/** A command forwarded to another node whose reply has not arrived yet. */
struct PendingCommand {
    /** Node that runs the command. */
    uint16_t dst = 0;
    /** Request id on LoRa, echoed by the reply. */
    uint8_t requestId = 0;
    /** Channel and request id the command came from, where its reply goes. */
    Origin origin = Origin::SERIAL_CONSOLE;
    uint8_t originRequestId = 0;
    /** A signed command may be sent twice: the node runs each counter once. */
    bool isSigned = false;
    /** The encoded command frame, kept for the retransmission. */
    std::vector<uint8_t> frame;
    /** Time the reply is waited for. */
    uint32_t timeoutMs = COMMAND_MIN_TIMEOUT_MS;
    /** Clock when the command was first sent. */
    uint32_t sentMs = 0;
    bool retransmitted = false;
};

/**
 * @brief The forwarded commands that wait for a reply, in a fixed number of slots.
 *
 * The caller provides the clock in milliseconds, which may wrap. The table is not thread
 * safe: one task owns it.
 */
class PendingCommands {
public:
    explicit PendingCommands(size_t slots);

    /**
     * @param superframeMs duration of the mesh superframe, 0 when unknown.
     * @return the time to wait for a reply: COMMAND_TIMEOUT_SUPERFRAMES superframes and at
     *         least COMMAND_MIN_TIMEOUT_MS.
     */
    static uint32_t timeoutMs(uint32_t superframeMs);

    /** @return the next request id that no waiting command uses. */
    uint8_t nextRequestId();

    /**
     * @brief Stores @p command, sent at @p nowMs, in the first free slot. A full table gives
     *        up its oldest command.
     * @param evicted the command that lost its slot.
     * @return true if a command was evicted.
     */
    bool add(const PendingCommand& command, uint32_t nowMs, PendingCommand& evicted);

    /**
     * @brief Matches the reply of node @p src with request id @p requestId and frees its slot.
     * @return false if no command waits for it: the reply is late, repeated or unknown.
     */
    bool take(uint16_t src, uint8_t requestId, PendingCommand& out);

    /**
     * @brief Finds what is due at @p nowMs. A signed command that has waited half its timeout
     *        is listed once in @p retransmit and stays. A command that has waited its whole
     *        timeout is listed in @p expired and leaves the table.
     */
    void sweep(uint32_t nowMs, std::vector<PendingCommand>& retransmit,
               std::vector<PendingCommand>& expired);

    /** @return the number of waiting commands. */
    size_t size() const;

private:
    struct Slot {
        bool used = false;
        PendingCommand command;
    };

    std::vector<Slot> slots_;
    uint8_t lastRequestId_ = 0;
};
