#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

/**
 * @brief Status of a command reply.
 *
 * 0 is success. 1 to 127 are generic errors with one table for the whole firmware. 128 to 255
 * are errors of the command that was sent, declared with that command.
 */
enum class CommandStatus : uint8_t {
    OK = 0,
    UNKNOWN = 1,
    PERM = 2,
    AUTH = 3,
    REPLAY = 4,
    ARG = 5,
    STORAGE = 6,
    /** 7 to 10 are produced by the gateway and never travel over LoRa. */
    NO_ROUTE = 7,
    TIMEOUT = 8,
    DROPPED = 9,
    TOO_LONG = 10,
    BUSY = 11,
};

/** First status that belongs to the command that was sent. */
constexpr uint8_t COMMAND_STATUS_SPECIFIC_FIRST = 128;

/** One row of the generic table. */
struct CommandStatusInfo {
    uint8_t code;
    /** Short name, as listed by /cmds. */
    const char* name;
    /** Text shown to people. */
    const char* text;
};

/** @return the generic table, ordered by code. @p count gets its length. */
const CommandStatusInfo* commandStatusTable(size_t& count);

/** @return the row of generic error @p code, or nullptr for 0, a specific or an unknown code. */
const CommandStatusInfo* commandStatusInfo(uint8_t code);

/** The outcome of a command: a status, and the reply text or the argument of an error. */
struct CommandResult {
    uint8_t status = 0;
    std::string text;

    CommandResult() = default;
    // NOLINTNEXTLINE(google-explicit-constructor): a plain text is a successful reply.
    CommandResult(std::string replyText) : text(std::move(replyText)) {}
    // NOLINTNEXTLINE(google-explicit-constructor): a plain text is a successful reply.
    CommandResult(const char* replyText) : text(replyText) {}
    CommandResult(uint8_t errorStatus, std::string argument)
        : status(errorStatus), text(std::move(argument)) {}
    // NOLINTNEXTLINE(google-explicit-constructor): a generic error needs no argument.
    CommandResult(CommandStatus errorStatus, std::string argument = "")
        : status(static_cast<uint8_t>(errorStatus)), text(std::move(argument)) {}

    bool ok() const { return status == 0; }
};

/** @return the MQTT form of an error: "E<code>" or "E<code> <argument>". */
std::string formatCommandError(uint8_t code, const std::string& argument);

/**
 * @brief Reads the MQTT form of an error.
 * @return false if @p text is a plain reply: it does not start with "E<code>" followed by the
 *         end or a space, or the code is 0 or above 255.
 */
bool parseCommandError(const std::string& text, uint8_t& code, std::string& argument);

/**
 * @return the payload of cmd-resp for request @p requestId: "<request id> <text>" for a
 *         success, "<request id> E<code>[ <argument>]" for an error.
 */
std::string formatReplyPayload(uint8_t requestId, const CommandResult& result);

/** @return true if @p text starts like an error ("E" and a digit). No reply text may. */
bool looksLikeCommandError(const std::string& text);

/** @return the line of /cmds that lists the generic errors: "E|1=unknown;2=perm;...". */
std::string commandStatusCatalogueLine();

/**
 * @return what a person reads for generic error @p code: its text, with the argument where the
 *         code has one. Empty for a code outside the generic table.
 */
std::string describeCommandStatus(uint8_t code, const std::string& argument);
