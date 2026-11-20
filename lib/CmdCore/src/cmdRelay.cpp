#include "cmdRelay.h"

#include <algorithm>

namespace {

BinaryCommandFrame replyWith(CommandStatus status) {
    BinaryCommandFrame reply;
    reply.response = true;
    reply.status = static_cast<uint8_t>(status);
    return reply;
}

BinaryCommandFrame replyWith(const BinaryReply& outcome) {
    BinaryCommandFrame reply;
    reply.response = true;
    reply.status = outcome.status;
    reply.payload = outcome.payload;
    reply.replayed = outcome.replayed;
    return reply;
}

ForwardPlan refused(ForwardPlan plan, CommandStatus status, const std::string& argument = "") {
    plan.status = status;
    plan.argument = argument;
    plan.frame.clear();
    return plan;
}

/** @return the plan, or TOO_LONG if its frame has more than @p maxFrame bytes. */
ForwardPlan fitted(const ForwardPlan& plan, size_t variableBytes, size_t maxFrame) {
    if (plan.frame.size() <= maxFrame) return plan;
    const size_t overhead = plan.frame.size() - variableBytes;
    return refused(plan, CommandStatus::TOO_LONG,
                   std::to_string(maxFrame > overhead ? maxFrame - overhead : 0));
}

}  // namespace

constexpr size_t BinaryCapableNodes::DEFAULT_CAPACITY;

BinaryCapableNodes::BinaryCapableNodes(size_t capacity) : capacity_(capacity) {}

void BinaryCapableNodes::note(uint16_t node, bool capable) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find(nodes_.begin(), nodes_.end(), node);
    if (!capable) {
        if (found != nodes_.end()) nodes_.erase(found);
        return;
    }
    if (found != nodes_.end() || capacity_ == 0) return;
    if (nodes_.size() >= capacity_) nodes_.erase(nodes_.begin());
    nodes_.push_back(node);
}

bool BinaryCapableNodes::has(uint16_t node) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::find(nodes_.begin(), nodes_.end(), node) != nodes_.end();
}

size_t BinaryCapableNodes::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.size();
}

ForwardPlan planForward(const CommandLine& command, const std::string& args,
                        const CommandSpec* spec, bool binaryNode, size_t maxFrame) {
    ForwardPlan plan;
    std::string lineArgs;
    splitCommand(command.line, plan.name, lineArgs);
    if (spec != nullptr && spec->id == 0 && spec->perm != Perm::LOCAL_ONLY) {
        return refused(plan, CommandStatus::NOT_REMOTE);
    }
    // The node checks a signature against the canonical line, in either form of the frame.
    Schema schema;
    const bool typed = spec != nullptr && spec->id != 0 && parseSchema(spec->args, schema);
    if (typed && command.isSigned && !isCanonicalLine(command.line, spec->name, schema)) {
        return refused(plan, CommandStatus::ARG);
    }
    if (!binaryNode) {
        CommandFrame frame;
        frame.line = command.line;
        frame.isSigned = command.isSigned;
        frame.counter = command.counter;
        frame.tag = command.tag;
        plan.frame = frame.encode();
        return fitted(plan, frame.line.size(), maxFrame);
    }

    if (spec == nullptr) return refused(plan, CommandStatus::UNKNOWN);
    if (spec->id == 0) return refused(plan, CommandStatus::PERM);
    if (!typed || !parseArgs(schema, args, plan.args)) return refused(plan, CommandStatus::ARG);
    BinaryCommandFrame frame;
    frame.id = spec->id;
    frame.isSigned = command.isSigned;
    frame.counter = command.counter;
    frame.tag = command.tag;
    if (!encodeFields(schema, plan.args, frame.payload)) return refused(plan, CommandStatus::ARG);
    plan.binary = true;
    plan.frame = frame.encode();
    return fitted(plan, frame.payload.size(), maxFrame);
}

CommandResult renderBinaryReply(const BinaryCommandFrame& reply,
                                const TypedDeclaration* declaration, const Fields& args) {
    if (reply.replayed && reply.status == 0) return CommandResult(COMMAND_ALREADY_EXECUTED_TEXT);
    TypedResult result;
    Fields out;
    if (declaration != nullptr && decodeTypedReply(*declaration, reply.status, reply.payload.data(),
                                                   reply.payload.size(), result, out)) {
        const CommandResult text = typedReplyText(*declaration, args, result, out);
        // BUSY here is of the gateway, which could not render fields that it decoded.
        if (reply.status != 0 || text.ok()) return text;
    }
    const std::string bytes = toHex(reply.payload.data(), reply.payload.size());
    if (reply.status != 0) return CommandResult(reply.status, bytes);
    return CommandResult("Reply not understood: " + bytes);
}

bool binaryRequestLine(const BinaryCommandFrame& request, const ServedCommand& command, Fields& in,
                       std::string& line) {
    Fields fields;
    if (!decodeFields(command.declaration.args, request.payload.data(), request.payload.size(),
                      fields) ||
        !canonicalLine(command.name, command.declaration.args, fields, line)) {
        return false;
    }
    in = fields;
    return true;
}

BinaryCommandFrame serveBinaryCommand(const BinaryCommandFrame& request,
                                      const ServedCommand* command, Origin origin,
                                      const NodeAuth& auth, size_t maxFrame) {
    if (command == nullptr) return replyWith(CommandStatus::UNKNOWN);
    Fields in;
    std::string line;
    if (!binaryRequestLine(request, *command, in, line)) return replyWith(CommandStatus::ARG);

    const size_t room =
        maxFrame > BinaryCommandFrame::HEADER_SIZE ? maxFrame - BinaryCommandFrame::HEADER_SIZE : 0;
    const std::function<BinaryReply()> run = [command, &in, room]() {
        BinaryReply reply;
        if (!command->handler) {
            reply.status = static_cast<uint8_t>(CommandStatus::UNKNOWN);
            return reply;
        }
        Fields out;
        const TypedResult result = command->handler(in, out);
        reply.status = result.status;
        if (!encodeTypedReplyWithin(command->declaration, result, out, room, reply.payload)) {
            reply.status = static_cast<uint8_t>(CommandStatus::BUSY);
            reply.payload.clear();
        }
        return reply;
    };

    if (!request.isSigned) {
        if (!commandPermitted(command->perm, origin, false)) return replyWith(CommandStatus::PERM);
        return replyWith(run());
    }
    if (!auth.hmac) return replyWith(CommandStatus::AUTH);
    const CommandTag expected = commandTag(auth.hmac, auth.address, request.counter, line);
    if (!commandTagsEqual(expected, request.tag)) return replyWith(CommandStatus::AUTH);
    if (!commandPermitted(command->perm, origin, true)) return replyWith(CommandStatus::PERM);
    CommandCounter* counter = auth.counter ? auth.counter() : nullptr;
    if (counter == nullptr) return replyWith(CommandStatus::STORAGE);
    return replyWith(runBinaryOncePerCounter(*counter, request.counter, auth.store, run));
}
