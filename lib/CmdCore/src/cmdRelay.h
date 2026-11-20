#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "cmdAuth.h"
#include "cmdCatalogue.h"
#include "cmdFrame.h"
#include "cmdPerm.h"
#include "cmdStatus.h"
#include "cmdText.h"
#include "cmdTyped.h"

/**
 * @file
 * @brief Binary commands between a gateway and a node: what the gateway sends for a command
 *        line, how a node serves a binary frame, and how the gateway reads the reply.
 *
 * A node answers in the form of the request. A gateway sends the binary form only to nodes that
 * announced it in their monitor report (BinaryCapableNodes), and text to every other node.
 */

/**
 * @brief The nodes that announced that they understand binary command frames.
 *
 * Kept in RAM, so after a restart every node counts as a text node until its next report. A
 * full table forgets the node it learned first. Any task may call any method.
 */
class BinaryCapableNodes {
public:
    explicit BinaryCapableNodes(size_t capacity = DEFAULT_CAPACITY);

    /** Records what the monitor report of @p node says. */
    void note(uint16_t node, bool capable);

    /** @return true if the last report of @p node announced binary commands. */
    bool has(uint16_t node) const;

    size_t size() const;

private:
    static constexpr size_t DEFAULT_CAPACITY = 64;

    size_t capacity_;
    mutable std::mutex mutex_;
    std::vector<uint16_t> nodes_;
};

/** What a gateway does with a command for another node. */
struct ForwardPlan {
    /** OK when @p frame is to be sent. Any other status is the answer of the gateway itself. */
    CommandStatus status = CommandStatus::OK;
    /** The argument of that answer, for TOO_LONG the most bytes that fit. */
    std::string argument;
    /** Name of the command as typed. */
    std::string name;
    /** The frame is a binary one. */
    bool binary = false;
    std::vector<uint8_t> frame;
    /** The typed arguments of a binary frame. */
    Fields args;

    bool send() const { return status == CommandStatus::OK; }
};

/**
 * @brief Decides how a command line goes to another node.
 *
 * - A command the gateway knows that has no id and is not local, such as /help, is refused with
 *   NOT_REMOTE for every node.
 * - A signed line of a command with an id must be the canonical one (isCanonicalLine()), since
 *   a binary node checks the signature against the line it rebuilds. Otherwise the answer is
 *   ARG, for every node, so the outcome does not depend on what the node announced.
 * - A text node gets the line as a text frame.
 * - A binary node gets the id and the typed arguments. A name the gateway does not know is
 *   refused with UNKNOWN, a local command with PERM, and arguments that do not fit the schema
 *   with ARG.
 * - A frame longer than @p maxFrame is refused with TOO_LONG.
 *
 * @param command the line with its signature.
 * @param args the arguments to read for a binary frame: those of the line, or for a line
 *        without a signature what the command made of them (the word "default" as 0).
 * @param spec the command of that name in the catalogue of the gateway, or nullptr.
 * @param binaryNode the node announced binary commands.
 */
ForwardPlan planForward(const CommandLine& command, const std::string& args,
                        const CommandSpec* spec, bool binaryNode, size_t maxFrame);

/**
 * @brief The text outcome of the reply to a forwarded binary command.
 * @param declaration the declaration of the command, or nullptr if the gateway has none.
 * @param args the typed arguments of the request.
 * @return the rendered reply, the status with its argument, or for a success that cannot be
 *         read a text that holds the bytes in hex.
 */
CommandResult renderBinaryReply(const BinaryCommandFrame& reply,
                                const TypedDeclaration* declaration, const Fields& args);

/** A command of a node, as a binary frame names it. */
struct ServedCommand {
    std::string name;
    Perm perm = Perm::LOCAL_ONLY;
    TypedDeclaration declaration;
    TypedHandler handler;
};

/** What a node checks a signed frame against. */
struct NodeAuth {
    uint16_t address = 0;
    /** HMAC under the deployment key. Empty on a node without a key. */
    KeyedHmac hmac;
    /** @return the replay counter, loaded from storage, or nullptr if it cannot be read. */
    std::function<CommandCounter*()> counter;
    /** Persists a counter before its command runs. */
    std::function<bool(uint32_t)> store;
};

/**
 * @brief Serves a binary request on a node.
 *
 * The answer is UNKNOWN for an id the node has no command for and ARG for fields that do not
 * fit its schema. A signed frame is checked like a signed text line: the tag covers the
 * canonical line built from the id and the fields (AUTH), and the counter lets the command run
 * once (REPLAY, STORAGE, or the stored reply for the last counter again). A frame without a
 * signature runs if the command is open (PERM otherwise). A reply whose last field is a group
 * loses rows until it fits @p maxFrame. Any other reply that does not fit gives BUSY.
 *
 * @param command the command with the id of the request, or nullptr.
 * @return the reply frame.
 */
BinaryCommandFrame serveBinaryCommand(const BinaryCommandFrame& request,
                                      const ServedCommand* command, Origin origin,
                                      const NodeAuth& auth, size_t maxFrame);

/**
 * @brief Builds the canonical line of a binary request.
 * @return false if the fields do not fit the schema of @p command.
 */
bool binaryRequestLine(const BinaryCommandFrame& request, const ServedCommand& command, Fields& in,
                       std::string& line);
