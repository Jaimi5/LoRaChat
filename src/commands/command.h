#pragma once

#include "Arduino.h"

#include <functional>
#include <memory>
#include <utility>

#include "cmdCatalogue.h"
#include "cmdPerm.h"
#include "cmdStatus.h"
#include "cmdTyped.h"

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

/**
 * @brief A command: its name, help text, arguments, permission, errors and handler.
 *
 * A text command has a text handler, which gets the arguments as typed and returns the reply
 * text. A typed command has an id, the schemas of its arguments and of its reply, a reply
 * template and a typed handler (cmdTyped.h). Its text reply is the template filled with the
 * fields the typed handler wrote, so a text line and a binary frame give the same text.
 *
 * A typed command gets a text handler next to the typed one only when a text line accepts
 * something that the typed arguments do not carry. The text handler sees every text line and
 * hands the lines it does not take to the typed handler with runTyped().
 *
 * A reply field of type "e(word)?" works as a marker: the handler sets it or leaves it absent,
 * and the template shows the bracket that names it. This gives one command several reply texts.
 */
class Command {
public:
    using Callback = std::function<CommandReply(String)>;
    using TypedCallback = TypedHandler;
    /** The text handler of a typed command. It gets the arguments as typed and the command. */
    using TextCallback = std::function<CommandReply(String, const Command&)>;

    Command() = default;

    Command(String command, String description, Perm perm, Callback callback)
        : command(std::move(command)),
          description(std::move(description)),
          callback(std::move(callback)),
          perm(perm) {}

    /** A typed command. @p id is its id in a binary frame, from the block of its service. */
    Command(uint8_t id, String command, String description, Perm perm)
        : command(std::move(command)), description(std::move(description)), perm(perm) {
        typed().id = id;
        declare();
    }

    /**
     * @brief Declares the arguments with a schema, as cmdFields.h describes it. The usage text
     *        comes from the schema unless withUsage() sets one.
     */
    Command& args(const char* schema) {
        typed().args = schema;
        declare();
        if (usage.length() == 0 && typed_->valid) {
            usage = usageFromSchema(typed_->declaration.args).c_str();
        }
        return *this;
    }

    /** Declares the reply fields with a schema, and the template that makes their text. */
    Command& reply(const char* schema, const char* replyTemplate) {
        typed().reply = schema;
        typed().replyTemplate = replyTemplate;
        declare();
        return *this;
    }

    /** Sets the typed handler. */
    Command& run(TypedCallback handler) {
        typed().handler = std::move(handler);
        return *this;
    }

    /** Sets the handler that sees the text lines of a typed command first. */
    Command& runText(TextCallback handler) {
        textCallback = std::move(handler);
        return *this;
    }

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

    /**
     * @brief Declares an error of this command: a status from 128 to 255 and its text for
     *        people.
     * @param detail how the number that a typed handler sends with the error reads: "" for
     *        decimal, "x" for hex as an esp_err_t code, "e(a,b)" for a name (parseErrorDetail()).
     */
    Command& withCode(uint8_t code, const char* text, const char* detail = "") {
        codes.push_back(CommandCode{code, text, detail});
        if (typed_) declare();
        return *this;
    }

    /** Runs the command for a text line. @p argText is the line without the command name. */
    CommandReply execute(const String& argText) const {
        if (callback) return callback(argText);
        if (textCallback) return textCallback(argText, *this);
        return runTyped(argText);
    }

    /** Reads @p argText as the typed arguments, runs the typed handler and renders its reply. */
    CommandReply runTyped(const String& argText) const {
        if (!typed_ || !typed_->valid) return CommandStatus::UNKNOWN;
        return runTypedFromText(typed_->declaration, typed_->handler, argText.c_str());
    }

    /** @return the text of a typed outcome that a text handler produced without arguments. */
    CommandReply render(const TypedResult& result, const Fields& out) const {
        if (!typed_ || !typed_->valid) return CommandStatus::UNKNOWN;
        return typedReplyText(typed_->declaration, Fields(), result, out);
    }

    /** Runs the typed handler. @return UNKNOWN for a command without one. */
    TypedResult executeTyped(const Fields& in, Fields& out) const {
        if (!typed_ || !typed_->handler) return CommandStatus::UNKNOWN;
        return typed_->handler(in, out);
    }

    /** @return the id in a binary frame, or 0 for a command without one. */
    uint8_t getId() const { return typed_ ? typed_->id : 0; }

    /** @return the typed declaration, or nullptr if the command has none or it is not valid. */
    const TypedDeclaration* declaration() const {
        return typed_ && typed_->valid ? &typed_->declaration : nullptr;
    }

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
        if (typed_) {
            out.id = typed_->id;
            out.args = typed_->args;
            out.reply = typed_->reply;
            out.replyTemplate = typed_->replyTemplate;
        }
        return out;
    }

private:
    /** The typed side of a command, shared by the copies of the command. */
    struct Typed {
        uint8_t id = 0;
        std::string args;
        std::string reply;
        std::string replyTemplate;
        TypedDeclaration declaration;
        bool valid = false;
        TypedCallback handler;
    };

    Typed& typed() {
        if (!typed_) typed_ = std::make_shared<Typed>();
        return *typed_;
    }

    void declare() { typed_->valid = typedDeclarationOf(spec(), typed_->declaration); }

    String command;
    String description;
    String usage;
    Callback callback;
    TextCallback textCallback;
    Perm perm = Perm::LOCAL_ONLY;
    uint32_t allowanceMs = 0;
    std::vector<CommandCode> codes;
    std::shared_ptr<Typed> typed_;
};
