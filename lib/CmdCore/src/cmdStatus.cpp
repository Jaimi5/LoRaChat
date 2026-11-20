#include "cmdStatus.h"

namespace {

const CommandStatusInfo STATUS_TABLE[] = {
    {1, "unknown", "Unknown command"},
    {2, "perm", "This command needs a signature"},
    {3, "auth", "The signature is not valid"},
    {4, "replay", "An older command than the last one. Not run"},
    {5, "arg", "Bad argument"},
    {6, "storage", "The node could not store the command counter"},
    {7, "no route", "The gateway has no route to the node"},
    {8, "timeout", "No reply within the time the gateway waited"},
    {9, "dropped", "Dropped by the gateway, too many commands waiting"},
    {10, "too long", "The command does not fit in one LoRa frame"},
    {11, "busy", "The node cannot run this now"},
    {12, "cancelled", "Cancelled before a reply arrived"},
};

constexpr size_t STATUS_COUNT = sizeof(STATUS_TABLE) / sizeof(STATUS_TABLE[0]);
constexpr unsigned MAX_CODE = 255;

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

}  // namespace

const CommandStatusInfo* commandStatusTable(size_t& count) {
    count = STATUS_COUNT;
    return STATUS_TABLE;
}

const CommandStatusInfo* commandStatusInfo(uint8_t code) {
    for (const CommandStatusInfo& info : STATUS_TABLE) {
        if (info.code == code) return &info;
    }
    return nullptr;
}

std::string formatCommandError(uint8_t code, const std::string& argument) {
    std::string text = "E" + std::to_string(static_cast<unsigned>(code));
    if (!argument.empty()) text += " " + argument;
    return text;
}

bool parseCommandError(const std::string& text, uint8_t& code, std::string& argument) {
    if (!looksLikeCommandError(text)) return false;
    size_t end = 1;
    unsigned value = 0;
    while (end < text.size() && isDigit(text[end])) {
        value = value * 10 + static_cast<unsigned>(text[end] - '0');
        if (value > MAX_CODE) return false;
        end++;
    }
    if (value == 0 || (end < text.size() && text[end] != ' ')) return false;
    code = static_cast<uint8_t>(value);
    argument = end < text.size() ? text.substr(end + 1) : "";
    return true;
}

std::string formatReplyPayload(uint8_t requestId, const CommandResult& result) {
    return std::to_string(static_cast<unsigned>(requestId)) + " " +
           (result.ok() ? result.text : formatCommandError(result.status, result.text));
}

bool looksLikeCommandError(const std::string& text) {
    return text.size() >= 2 && text[0] == 'E' && isDigit(text[1]);
}

std::string commandStatusCatalogueLine() {
    std::string line = "E|";
    for (size_t i = 0; i < STATUS_COUNT; i++) {
        if (i > 0) line += ";";
        line += std::to_string(static_cast<unsigned>(STATUS_TABLE[i].code));
        line += "=";
        line += STATUS_TABLE[i].name;
    }
    return line;
}

std::string describeCommandStatus(uint8_t code, const std::string& argument) {
    const CommandStatusInfo* info = commandStatusInfo(code);
    if (info == nullptr) return "";
    std::string text = info->text;
    if (argument.empty()) return text;
    if (code == static_cast<uint8_t>(CommandStatus::TIMEOUT)) return text + " (" + argument + " s)";
    if (code == static_cast<uint8_t>(CommandStatus::TOO_LONG)) {
        return text + " (max " + argument + " B)";
    }
    return text + " (" + argument + ")";
}
