#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cmdPerm.h"

/** Margin added to every wait for a reply (g). */
constexpr uint32_t COMMAND_GUARD_MS = 2000;
/** Time a command that timed out is remembered, so its late reply is still delivered. */
constexpr uint32_t COMMAND_LATE_REPLY_MS = 600000;
/** Timed out commands remembered at most. */
constexpr size_t COMMAND_LATE_SLOTS = 16;

/** What the wait for one transmission of a command depends on. */
struct AttemptTiming {
    /** Time until the gateway transmits the command (w): its next TX slot and its queue. */
    uint32_t slotWaitMs = 0;
    /** Hops from the gateway to the node (h). */
    uint8_t hops = 0;
    /** Duration of the mesh superframe (S), 0 when unknown. */
    uint32_t superframeMs = 0;
    /** Time the command may take to run on the node (P). */
    uint32_t allowanceMs = 0;
};

/**
 * @return the time to wait for the reply to one transmission:
 *         A = w + (h + 1) * S + ceil(P / S) * S + g. A lossless reply never takes longer.
 *         Without a superframe the allowance counts as it is.
 */
uint32_t attemptTimeMs(const AttemptTiming& timing);

/**
 * @return the time after which the reply is expected: w + SRTT + g when the node has a measured
 *         estimate, otherwise w + h * S.
 */
uint32_t expectedReplyMs(const AttemptTiming& timing, bool measured, uint32_t srttMs);

/** @return @p ms in whole seconds, rounded up. */
uint32_t ceilSeconds(uint32_t ms);

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
    /** Name of the command, which scopes the specific errors of its reply. */
    std::string name;
    /** The encoded command frame, kept for the retransmission while the command waits. */
    std::vector<uint8_t> frame;
    /** Timing of the first transmission. */
    AttemptTiming timing;
    /** Neighbour the command left through. */
    uint16_t nextHop = 0;
    /** Clock when the first transmission was requested. */
    uint32_t sentMs = 0;
    /** Time from sentMs before which nothing is sent again (A1). Set by the table. */
    uint32_t attemptMs = 0;
    /** Time from sentMs at which the command is given up. Set by the table. */
    uint32_t giveUpMs = 0;
    /** Time from sentMs at which the reply is expected. */
    uint32_t expectedMs = 0;
    bool retransmitted = false;

    /** @return the time waited at @p nowMs. */
    uint32_t waitedMs(uint32_t nowMs) const { return nowMs - sentMs; }
    /** @return the time left at @p nowMs until the command is given up. */
    uint32_t remainingMs(uint32_t nowMs) const;
    /** @return the time left at @p nowMs until the reply is expected. */
    uint32_t expectedInMs(uint32_t nowMs) const;
    /** @return the transmissions so far, 1 or 2. */
    unsigned attempts() const { return retransmitted ? 2 : 1; }
};

/**
 * @return the cmd-ack line for a command the gateway queued, or was asked for again:
 *         "<request id> Q <giveup_s> <expected_s> <hops> <superframe_ms>", times from @p nowMs.
 */
std::string formatQueuedAck(const PendingCommand& command, uint32_t nowMs);

/** @return the cmd-ack line of the retransmission: "<request id> R <giveup_s>". */
std::string formatRetransmitAck(const PendingCommand& command, uint32_t nowMs);

/** @return the cmd-ack line after a reply: "<request id> D <rtt_ms> <attempts>". */
std::string formatDoneAck(const PendingCommand& command, uint32_t nowMs);

/**
 * @brief The forwarded commands that wait for a reply, in a fixed number of slots.
 *
 * A command is never sent again while a lossless reply to it may still be on its way. A signed
 * command waits A1, is sent once more and is given up A2 later. An unsigned command is given up
 * after A. A command that was given up is remembered without its frame for
 * COMMAND_LATE_REPLY_MS, so a late reply still finds it.
 *
 * The caller provides the clock in milliseconds, which may wrap. The table is not thread
 * safe: one task owns it.
 */
class PendingCommands {
public:
    explicit PendingCommands(size_t slots, size_t lateSlots = COMMAND_LATE_SLOTS);

    /** @return the next request id that no waiting or remembered command uses. */
    uint8_t nextRequestId(uint32_t nowMs);

    /**
     * @brief Stores @p command, sent at @p nowMs, in the first free slot and sets its times
     *        from its timing, also in @p command. Until the second transmission the give-up
     *        time of a signed command assumes that it waits one superframe for its slot. A full
     *        table gives up its oldest command.
     * @param evicted the command that lost its slot.
     * @return true if a command was evicted.
     */
    bool add(PendingCommand& command, uint32_t nowMs, PendingCommand& evicted);

    /**
     * @return the waiting command that came from @p origin with the same destination, request
     *         id and frame, or nullptr. The pointer is valid until the table changes.
     */
    const PendingCommand* findRepeat(Origin origin, uint16_t dst, uint8_t originRequestId,
                                     const std::vector<uint8_t>& frame) const;

    /**
     * @brief Matches the reply of node @p src with request id @p requestId and frees its slot.
     * @return false if no command waits for it.
     */
    bool take(uint16_t src, uint8_t requestId, PendingCommand& out);

    /**
     * @brief Matches a reply with a command that was given up at most COMMAND_LATE_REPLY_MS
     *        ago, and forgets the command.
     * @return false if no such command is remembered.
     */
    bool takeLate(uint16_t src, uint8_t requestId, uint32_t nowMs, PendingCommand& out);

    /**
     * @brief Finds what is due at @p nowMs. A signed command that has waited A1 is listed once
     *        in @p retransmit and stays. A command that has waited its give-up time is listed
     *        in @p expired, leaves the table and is remembered for its late reply.
     */
    void sweep(uint32_t nowMs, std::vector<PendingCommand>& retransmit,
               std::vector<PendingCommand>& expired);

    /**
     * @brief Records the second transmission of a command at @p nowMs: it is given up
     *        attemptTimeMs(@p timing) from now and its reply is expected @p expectedInMs from
     *        now.
     * @param out the command with its new times.
     * @return false if the command does not wait any more.
     */
    bool secondAttempt(uint16_t dst, uint8_t requestId, uint32_t nowMs, const AttemptTiming& timing,
                       uint32_t expectedInMs, PendingCommand& out);

    /**
     * @brief Gives up a waiting command at @p nowMs and remembers it for its late reply.
     * @return false if the command does not wait any more.
     */
    bool giveUp(uint16_t dst, uint8_t requestId, uint32_t nowMs, PendingCommand& out);

    /** @return the number of waiting commands. */
    size_t size() const;

    /** @return the number of commands remembered for a late reply at @p nowMs. */
    size_t lateSize(uint32_t nowMs) const;

private:
    struct Slot {
        bool used = false;
        PendingCommand command;
        /** Clock when a remembered command was given up. */
        uint32_t expiredMs = 0;
    };

    void remember(const PendingCommand& command, uint32_t nowMs);
    void forgetStale(uint32_t nowMs);
    Slot* waiting(uint16_t dst, uint8_t requestId);

    std::vector<Slot> slots_;
    std::vector<Slot> late_;
    uint8_t lastRequestId_ = 0;
};
