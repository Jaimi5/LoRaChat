#include "cmdFrame.h"

constexpr uint8_t CommandFrame::FLAG_RESPONSE;
constexpr uint8_t CommandFrame::FLAG_SIGNED;
constexpr size_t CommandFrame::SIGNATURE_SIZE;

std::vector<uint8_t> CommandFrame::encode() const {
    std::vector<uint8_t> bytes;
    bytes.reserve(1 + line.size() + SIGNATURE_SIZE);
    bytes.push_back(static_cast<uint8_t>((response ? FLAG_RESPONSE : 0) |
                                         (isSigned ? FLAG_SIGNED : 0)));
    bytes.insert(bytes.end(), line.begin(), line.end());
    if (isSigned) {
        for (int shift = 0; shift < 32; shift += 8) {
            bytes.push_back(static_cast<uint8_t>((counter >> shift) & 0xFF));
        }
        bytes.insert(bytes.end(), tag.begin(), tag.end());
    }
    return bytes;
}

bool CommandFrame::decode(const uint8_t* data, size_t size, CommandFrame& out) {
    if (data == nullptr || size < 2) return false;
    CommandFrame frame;
    frame.response = (data[0] & FLAG_RESPONSE) != 0;
    frame.isSigned = (data[0] & FLAG_SIGNED) != 0;
    size_t lineEnd = size;
    if (frame.isSigned) {
        if (size < 2 + SIGNATURE_SIZE) return false;
        lineEnd = size - SIGNATURE_SIZE;
        for (int i = 3; i >= 0; i--) frame.counter = frame.counter << 8 | data[lineEnd + i];
        for (size_t i = 0; i < frame.tag.size(); i++) frame.tag[i] = data[lineEnd + 4 + i];
    }
    frame.line.assign(reinterpret_cast<const char*>(data + 1), lineEnd - 1);
    if (frame.line.find('\0') != std::string::npos) return false;
    out = frame;
    return true;
}
