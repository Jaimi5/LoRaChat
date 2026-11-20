#pragma once

#include "Arduino.h"

#include <functional>
#include <utility>
#include <vector>

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
 *
 * The description, the schemas, the template and the error texts are string literals, which the
 * command points at. The schemas are read each time they are needed, so a command holds no
 * parsed form of them.
 */
class Command {
public:
    using Callback = std::function<CommandReply(String)>;
    using TypedCallback = TypedHandler;
    /** The text handler of a typed command. It gets the arguments as typed and the command. */
    using TextCallback = std::function<CommandReply(String, const Command&)>;
    /** Turns arguments a person may type into the typed ones. */
    using TextAlias = String (*)(const String&);

    Command() = default;

    /** @param description a string literal, or a text that lives as long as the command. */
    Command(String command, const char* description, Perm perm, Callback callback)
        : command(std::move(command)),
          description(description),
          callback(std::move(callback)),
          perm(perm) {}

    /**
     * @brief A typed command. @p id is its id in a binary frame, from the block of its service.
     * @param description a string literal, or a text that lives as long as the command.
     */
    Command(uint8_t id, String command, const char* description, Perm perm)
        : command(std::move(command)), description(description), perm(perm), id(id) {}

    /**
     * @brief Declares the arguments with a schema, as cmdFields.h describes it. The usage text
     *        comes from the schema unless withUsage() sets one.
     * @param schema a string literal.
     */
    Command& args(const char* schema) {
        argsSchema = schema;
        Schema parsed;
        if (usage.length() == 0 && parseSchema(schema, parsed)) {
            usage = usageFromSchema(parsed).c_str();
        }
        return *this;
    }

    /**
     * @brief Declares the reply fields with a schema, and the template that makes their text.
     *        Both are string literals.
     */
    Command& reply(const char* schema, const char* textTemplate) {
        replySchema = schema;
        replyTemplate = textTemplate;
        return *this;
    }

    /** Sets the typed handler. */
    Command& run(TypedCallback handler) {
        typedCallback = std::move(handler);
        return *this;
    }

    /** Sets the handler that sees the text lines of a typed command first. */
    Command& runText(TextCallback handler) {
        textCallback = std::move(handler);
        return *this;
    }

    /**
     * @brief Sets what turns the arguments of a text line into the typed ones, for words that
     *        stand for a value. A signed line for another node never passes through it, since
     *        its signature covers the line as it is.
     */
    Command& withAlias(TextAlias alias) {
        textAlias = alias;
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
     *        people. Both texts are string literals.
     * @param detail how the number that a typed handler sends with the error reads: "" for
     *        decimal, "x" for hex as an esp_err_t code, "e(a,b)" for a name (parseErrorDetail()).
     */
    Command& withCode(uint8_t code, const char* text, const char* detail = "") {
        codes.push_back(Code{code, text, detail});
        return *this;
    }

    /** Runs the command for a text line. @p argText is the line without the command name. */
    CommandReply execute(const String& argText) const {
        if (callback) return callback(argText);
        if (textCallback) return textCallback(argText, *this);
        return runTyped(argText);
    }

    /**
     * @brief Reads @p argText as the typed arguments, after the alias of the command, runs the
     *        typed handler and renders its reply.
     */
    CommandReply runTyped(const String& argText) const {
        TypedDeclaration declared;
        if (!declaration(declared)) return CommandStatus::UNKNOWN;
        return runTypedFromText(declared, typedCallback, typedArgs(argText).c_str());
    }

    /** @return the text of a typed outcome that a text handler produced without arguments. */
    CommandReply render(const TypedResult& result, const Fields& out) const {
        TypedDeclaration declared;
        if (!declaration(declared)) return CommandStatus::UNKNOWN;
        return typedReplyText(declared, Fields(), result, out);
    }

    /** @return @p argText as the typed arguments read it: what the alias makes of it. */
    String typedArgs(const String& argText) const {
        return textAlias != nullptr ? textAlias(argText) : argText;
    }

    /** @return the typed handler, empty for a command without one. */
    const TypedCallback& typedHandler() const { return typedCallback; }

    /** @return the id in a binary frame, or 0 for a command without one. */
    uint8_t getId() const { return id; }

    /**
     * @brief Reads the typed declaration of the command.
     * @return false, leaving @p out unchanged, if the command has no id or its declaration is
     *         not valid.
     */
    bool declaration(TypedDeclaration& out) const {
        return id != 0 && typedDeclarationOf(spec(), out);
    }

    const String& getCommand() const { return command; }

    const char* getDescription() const { return description; }

    const String& getUsage() const { return usage; }

    Perm getPerm() const { return perm; }

    uint32_t getAllowanceMs() const { return allowanceMs; }

    /** @return the text of specific error @p code, or nullptr if the command declares none. */
    const char* codeText(uint8_t code) const {
        for (const Code& entry : codes) {
            if (entry.code == code) return entry.text;
        }
        return nullptr;
    }

    /** @return the command as the catalogue lists it. */
    CommandSpec spec() const {
        CommandSpec out;
        out.name = command.c_str();
        out.perm = perm;
        out.usage = usage.c_str();
        out.allowanceMs = allowanceMs;
        out.codes.reserve(codes.size());
        for (const Code& entry : codes) {
            out.codes.push_back(CommandCode{entry.code, entry.text, entry.detail});
        }
        out.id = id;
        out.args = argsSchema;
        out.reply = replySchema;
        out.replyTemplate = replyTemplate;
        return out;
    }

private:
    /** An error of the command. The texts are string literals. */
    struct Code {
        uint8_t code;
        const char* text;
        const char* detail;
    };

    String command;
    const char* description = "";
    String usage;
    Callback callback;
    TextCallback textCallback;
    TypedCallback typedCallback;
    TextAlias textAlias = nullptr;
    Perm perm = Perm::LOCAL_ONLY;
    uint8_t id = 0;
    uint32_t allowanceMs = 0;
    const char* argsSchema = "";
    const char* replySchema = "";
    const char* replyTemplate = "";
    std::vector<Code> codes;
};
