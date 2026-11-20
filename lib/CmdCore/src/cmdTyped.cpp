#include "cmdTyped.h"

namespace {

constexpr uint64_t DETAIL_MAX = 0xFFFFFFFFULL;

bool fitsCatalogue(const std::string& text) {
    return text.find_first_of("|;\r\n") == std::string::npos;
}

std::string hexNumber(uint32_t value) {
    static const char DIGITS[] = "0123456789abcdef";
    std::string digits;
    do {
        digits.insert(digits.begin(), DIGITS[value % 16]);
        value /= 16;
    } while (value > 0);
    return "0x" + digits;
}

}  // namespace

bool parseErrorDetail(uint8_t code, const std::string& type, ErrorDetail& out) {
    ErrorDetail detail;
    detail.code = code;
    if (type == "x") {
        detail.kind = ErrorDetailKind::HEX_NUMBER;
    } else if (!type.empty() && type != "u") {
        // The names are read as the names of an e field are.
        Schema schema;
        if (type[0] != 'e' || !parseSchema("d:" + type, schema) || schema.fields.size() != 1 ||
            schema.fields[0].optional) {
            return false;
        }
        detail.kind = ErrorDetailKind::NAME_OF;
        detail.names = schema.fields[0].names;
    }
    out = detail;
    return true;
}

std::string errorDetailText(const std::vector<ErrorDetail>& details, uint8_t status,
                            uint32_t detail) {
    for (const ErrorDetail& declared : details) {
        if (declared.code != status) continue;
        if (declared.kind == ErrorDetailKind::HEX_NUMBER) return hexNumber(detail);
        if (declared.kind == ErrorDetailKind::NAME_OF && detail < declared.names.size()) {
            return declared.names[detail];
        }
        break;
    }
    return std::to_string(detail);
}

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
        return CommandResult(
            result.status, result.hasDetail
                               ? errorDetailText(declaration.details, result.status, result.detail)
                               : "");
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

bool encodeTypedReplyWithin(const TypedDeclaration& declaration, const TypedResult& result,
                            const Fields& out, size_t maxBytes, std::vector<uint8_t>& payload,
                            size_t* keptRows) {
    Fields kept = out;
    for (;;) {
        std::vector<uint8_t> bytes;
        if (!encodeTypedReply(declaration, result, kept, bytes)) return false;
        if (bytes.size() <= maxBytes) {
            payload.insert(payload.end(), bytes.begin(), bytes.end());
            if (keptRows != nullptr) *keptRows = kept.rowCount();
            return true;
        }
        if (!result.ok() || kept.rowCount() == 0) return false;
        kept.dropLastRow();
    }
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
