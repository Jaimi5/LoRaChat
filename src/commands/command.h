#pragma once

#include "Arduino.h"

#include <functional>
#include <utility>

#include "cmdCatalogue.h"
#include "cmdPerm.h"
#include "cmdStatus.h"

/**
 * @brief What a command handler returns: a status, and the reply text or the argument of an
 *        error. A plain text is a successful reply.
 */
struct CommandReply {
    uint8_t status = 0;
    String text;

    CommandReply() = default;
    // NOLINTNEXTLINE(google-explicit-constructor): a plain text is a successful reply.
    CommandReply(const String& replyText) : text(replyText) {}
    // NOLINTNEXTLINE(google-explicit-constructor): a plain text is a successful reply.
    CommandReply(const char* replyText) : text(replyText) {}
    // NOLINTNEXTLINE(google-explicit-constructor): the result of pure command logic.
    CommandReply(const CommandResult& result) : status(result.status), text(result.text.c_str()) {}
    // NOLINTNEXTLINE(google-explicit-constructor): a generic error needs no argument.
    CommandReply(CommandStatus errorStatus, const String& argument = "")
        : status(static_cast<uint8_t>(errorStatus)), text(argument) {}
    /** An error of the command itself, with a code from 128 to 255. */
    CommandReply(uint8_t errorStatus, const String& argument)
        : status(errorStatus), text(argument) {}

    bool ok() const { return status == 0; }

    CommandResult result() const { return CommandResult(status, text.c_str()); }
};

/** A text command: its name, help text, arguments, permission, errors and handler. */
class Command {
public:
    using Callback = std::function<CommandReply(String)>;

    Command() = default;

    Command(String command, String description, Perm perm, Callback callback)
        : command(std::move(command)),
          description(std::move(description)),
          callback(std::move(callback)),
          perm(perm) {}

    /** Sets the argument description, without the command name. */
    Command& withUsage(const String& text) {
        usage = text;
        return *this;
    }

    /** Sets the time the command may take to run, in ms. A gateway waits that much longer. */
    Command& withAllowance(uint32_t ms) {
        allowanceMs = ms;
        return *this;
    }

    /** Declares an error of this command: a status from 128 to 255 and its text for people. */
    Command& withCode(uint8_t code, const char* text) {
        codes.push_back(CommandCode{code, text});
        return *this;
    }

    CommandReply execute(String args) { return callback(args); }

    const String& getCommand() const { return command; }

    const String& getDescription() const { return description; }

    const String& getUsage() const { return usage; }

    Perm getPerm() const { return perm; }

    /** @return the command as the catalogue lists it. */
    CommandSpec spec() const {
        CommandSpec out;
        out.name = command.c_str();
        out.perm = perm;
        out.usage = usage.c_str();
        out.allowanceMs = allowanceMs;
        out.codes = codes;
        return out;
    }

private:
    String command;
    String description;
    String usage;
    Callback callback;
    Perm perm = Perm::LOCAL_ONLY;
    uint32_t allowanceMs = 0;
    std::vector<CommandCode> codes;
};
