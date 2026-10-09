#include "cmdText.h"

#include <cstdlib>

namespace {

constexpr size_t ADDRESS_DIGITS = 4;
constexpr size_t COUNTER_DIGITS = 8;
constexpr char SPACES[] = " \t";

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string trim(const std::string& text) {
    size_t first = text.find_first_not_of(SPACES);
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(SPACES) - first + 1);
}

/** Parses exactly @p digits hex digits. */
bool parseHexNumber(const std::string& text, size_t digits, uint32_t& out) {
    if (text.size() != digits) return false;
    uint32_t value = 0;
    for (char c : text) {
        int digit = hexValue(c);
        if (digit < 0) return false;
        value = value << 4 | static_cast<uint32_t>(digit);
    }
    out = value;
    return true;
}

/** Parses "<counter>.<tag>". */
bool parseSignature(const std::string& text, uint32_t& counter, CommandTag& tag) {
    size_t dot = text.find('.');
    std::vector<uint8_t> bytes;
    if (dot == std::string::npos || !parseHexNumber(text.substr(0, dot), COUNTER_DIGITS, counter) ||
        !fromHex(text.substr(dot + 1), bytes) || bytes.size() != tag.size()) {
        return false;
    }
    for (size_t i = 0; i < tag.size(); i++) tag[i] = bytes[i];
    return true;
}

}  // namespace

bool parseCommandLine(const std::string& text, CommandLine& out) {
    CommandLine parsed;
    std::string rest = trim(text);
    if (!rest.empty() && rest[0] == '@') {
        uint32_t dst = 0;
        if (rest.size() <= ADDRESS_DIGITS + 1 || rest[ADDRESS_DIGITS + 1] != ' ' ||
            !parseHexNumber(rest.substr(1, ADDRESS_DIGITS), ADDRESS_DIGITS, dst)) {
            return false;
        }
        parsed.remote = true;
        parsed.dst = static_cast<uint16_t>(dst);
        rest = trim(rest.substr(ADDRESS_DIGITS + 2));
    }

    // A last word "#<counter>.<tag>" is a signature; any other '#' belongs to the line.
    size_t hash = rest.rfind('#');
    if (hash != std::string::npos && (hash == 0 || rest[hash - 1] == ' ')) {
        std::string word = rest.substr(hash + 1);
        if (word.find_first_of(SPACES) == std::string::npos && word.find('.') != std::string::npos) {
            if (!parseSignature(word, parsed.counter, parsed.tag)) return false;
            parsed.isSigned = true;
            rest = trim(rest.substr(0, hash));
        }
    }
    if (rest.empty()) return false;
    parsed.line = rest;
    out = parsed;
    return true;
}

std::string formatSignature(uint32_t counter, const CommandTag& tag) {
    static const char DIGITS[] = "0123456789abcdef";
    std::string text = " #";
    for (int shift = 28; shift >= 0; shift -= 4) text += DIGITS[(counter >> shift) & 0xF];
    return text + "." + toHex(tag.data(), tag.size());
}

void splitCommand(const std::string& line, std::string& name, std::string& args) {
    std::string text = trim(line);
    size_t space = text.find_first_of(SPACES);
    name = text.substr(0, space);
    args = space == std::string::npos ? "" : trim(text.substr(space));
}

bool parseMqttCommand(const std::string& text, MqttCommand& out) {
    std::string rest = trim(text);
    size_t first = rest.find(' ');
    size_t second = first == std::string::npos ? first : rest.find(' ', first + 1);
    if (second == std::string::npos) return false;

    std::string dstText = rest.substr(0, first);
    std::string idText = rest.substr(first + 1, second - first - 1);
    uint32_t dst = 0;
    if (dstText.empty() || dstText.size() > ADDRESS_DIGITS ||
        !parseHexNumber(std::string(ADDRESS_DIGITS - dstText.size(), '0') + dstText,
                        ADDRESS_DIGITS, dst)) {
        return false;
    }
    char* end = nullptr;
    unsigned long requestId = std::strtoul(idText.c_str(), &end, 10);
    if (idText.empty() || *end != '\0' || requestId > 0xFF) return false;

    MqttCommand parsed;
    if (!parseCommandLine(rest.substr(second + 1), parsed.command) || parsed.command.remote) {
        return false;
    }
    parsed.dst = static_cast<uint16_t>(dst);
    parsed.requestId = static_cast<uint8_t>(requestId);
    out = parsed;
    return true;
}

std::string toHex(const uint8_t* data, size_t size) {
    static const char DIGITS[] = "0123456789abcdef";
    std::string text;
    text.reserve(2 * size);
    for (size_t i = 0; i < size; i++) {
        text += DIGITS[data[i] >> 4];
        text += DIGITS[data[i] & 0xF];
    }
    return text;
}

bool fromHex(const std::string& text, std::vector<uint8_t>& out) {
    if (text.size() % 2 != 0) return false;
    std::vector<uint8_t> bytes;
    bytes.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2) {
        int high = hexValue(text[i]);
        int low = hexValue(text[i + 1]);
        if (high < 0 || low < 0) return false;
        bytes.push_back(static_cast<uint8_t>(high << 4 | low));
    }
    out = bytes;
    return true;
}
