#include "cmdTyped.h"

namespace {

constexpr uint64_t DETAIL_MAX = 0xFFFFFFFFULL;

bool fitsCatalogue(const std::string& text) {
    return text.find_first_of("|;\r\n") == std::string::npos;
}

}  // namespace

bool parseTypedDeclaration(uint8_t id, const std::string& args, const std::string& reply,
                           const std::string& replyTemplate, TypedDeclaration& out) {
    TypedDeclaration declaration;
    declaration.id = id;
    declaration.argsText = args;
    declaration.replyText = reply;
    declaration.replyTemplate = replyTemplate;
    if (id == 0 || !fitsCatalogue(args) || !fitsCatalogue(reply) || !fitsCatalogue(replyTemplate)) {
        return false;
    }
    if (!parseSchema(args, declaration.args) || declaration.args.hasGroup() ||
        !parseSchema(reply, declaration.reply) ||
        !checkTemplate(replyTemplate, declaration.reply, declaration.args)) {
        return false;
    }
    out = declaration;
    return true;
}

CommandResult typedReplyText(const TypedDeclaration& declaration, const Fields& in,
                             const TypedResult& result, const Fields& out) {
    if (!result.ok()) {
        return CommandResult(result.status, result.hasDetail ? std::to_string(result.detail) : "");
    }
    std::string text;
    if (!renderTemplate(declaration.replyTemplate, declaration.reply, out, declaration.args, in,
                        text)) {
        return CommandStatus::BUSY;
    }
    return CommandResult(text);
}

CommandResult runTypedFromText(const TypedDeclaration& declaration, const TypedHandler& handler,
                               const std::string& args) {
    Fields in;
    if (!parseArgs(declaration.args, args, in)) return CommandStatus::ARG;
    if (!handler) return CommandStatus::UNKNOWN;
    Fields out;
    const TypedResult result = handler(in, out);
    return typedReplyText(declaration, in, result, out);
}

bool encodeTypedReply(const TypedDeclaration& declaration, const TypedResult& result,
                      const Fields& out, std::vector<uint8_t>& payload) {
    if (result.ok()) return encodeFields(declaration.reply, out, payload);
    if (result.hasDetail) appendVarint(payload, result.detail);
    return true;
}

bool decodeTypedReply(const TypedDeclaration& declaration, uint8_t status, const uint8_t* payload,
                      size_t size, TypedResult& result, Fields& out) {
    if (status == 0) {
        if (!decodeFields(declaration.reply, payload, size, out)) return false;
        result = TypedResult();
        return true;
    }
    if (size == 0) {
        result = TypedResult(status);
        return true;
    }
    size_t pos = 0;
    uint64_t detail = 0;
    if (!readVarint(payload, size, pos, DETAIL_MAX, detail)) return false;
    result = TypedResult(status, static_cast<uint32_t>(detail));
    return true;
}
