#include "cmdFrame.h"

constexpr uint8_t CommandFrame::FLAG_RESPONSE;
constexpr uint8_t CommandFrame::FLAG_SIGNED;
constexpr size_t CommandFrame::SIGNATURE_SIZE;
constexpr size_t CommandFrame::REPLY_HEADER_SIZE;
constexpr uint8_t BinaryCommandFrame::FLAG_BINARY;
constexpr uint8_t BinaryCommandFrame::FLAG_REPLAYED;
constexpr size_t BinaryCommandFrame::HEADER_SIZE;

std::vector<uint8_t> CommandFrame::encode() const {
    std::vector<uint8_t> bytes;
    bytes.reserve(REPLY_HEADER_SIZE + line.size() + SIGNATURE_SIZE);
    bytes.push_back(static_cast<uint8_t>((response ? FLAG_RESPONSE : 0) |
                                         (isSigned ? FLAG_SIGNED : 0)));
    if (response) {
        bytes.push_back(status);
        bytes.insert(bytes.end(), line.begin(), line.end());
        return bytes;
    }
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
    if (frame.response) {
        if (frame.isSigned) return false;
        frame.status = data[1];
        frame.line.assign(reinterpret_cast<const char*>(data + REPLY_HEADER_SIZE),
                          size - REPLY_HEADER_SIZE);
        if (frame.line.find('\0') != std::string::npos) return false;
        out = frame;
        return true;
    }
    size_t lineEnd = size;
    if (frame.isSigned) {
        if (size < 2 + SIGNATURE_SIZE) return false;
        lineEnd = size - SIGNATURE_SIZE;
        for (size_t i = 4; i-- > 0;) frame.counter = frame.counter << 8 | data[lineEnd + i];
        for (size_t i = 0; i < frame.tag.size(); i++) frame.tag[i] = data[lineEnd + 4 + i];
    }
    frame.line.assign(reinterpret_cast<const char*>(data + 1), lineEnd - 1);
    if (frame.line.find('\0') != std::string::npos) return false;
    out = frame;
    return true;
}

std::vector<uint8_t> BinaryCommandFrame::encode() const {
    std::vector<uint8_t> bytes;
    bytes.reserve(HEADER_SIZE + payload.size() + CommandFrame::SIGNATURE_SIZE);
    if (response) {
        bytes.push_back(static_cast<uint8_t>(CommandFrame::FLAG_RESPONSE | FLAG_BINARY |
                                             (replayed ? FLAG_REPLAYED : 0)));
        bytes.push_back(status);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return bytes;
    }
    bytes.push_back(static_cast<uint8_t>(FLAG_BINARY | (isSigned ? CommandFrame::FLAG_SIGNED : 0)));
    bytes.push_back(id);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    if (isSigned) {
        for (int shift = 0; shift < 32; shift += 8) {
            bytes.push_back(static_cast<uint8_t>((counter >> shift) & 0xFF));
        }
        bytes.insert(bytes.end(), tag.begin(), tag.end());
    }
    return bytes;
}

bool BinaryCommandFrame::decode(const uint8_t* data, size_t size, BinaryCommandFrame& out) {
    if (!isBinaryCommandFrame(data, size) || size < HEADER_SIZE) return false;
    BinaryCommandFrame frame;
    frame.response = (data[0] & CommandFrame::FLAG_RESPONSE) != 0;
    frame.isSigned = (data[0] & CommandFrame::FLAG_SIGNED) != 0;
    if (frame.response) {
        if (frame.isSigned) return false;
        frame.replayed = (data[0] & FLAG_REPLAYED) != 0;
        frame.status = data[1];
        frame.payload.assign(data + HEADER_SIZE, data + size);
        out = frame;
        return true;
    }
    frame.id = data[1];
    if (frame.id == 0) return false;
    size_t fieldsEnd = size;
    if (frame.isSigned) {
        if (size < HEADER_SIZE + CommandFrame::SIGNATURE_SIZE) return false;
        fieldsEnd = size - CommandFrame::SIGNATURE_SIZE;
        for (size_t i = 4; i-- > 0;) frame.counter = frame.counter << 8 | data[fieldsEnd + i];
        for (size_t i = 0; i < frame.tag.size(); i++) frame.tag[i] = data[fieldsEnd + 4 + i];
    }
    frame.payload.assign(data + HEADER_SIZE, data + fieldsEnd);
    out = frame;
    return true;
}

bool isBinaryCommandFrame(const uint8_t* data, size_t size) {
    return data != nullptr && size > 0 && (data[0] & BinaryCommandFrame::FLAG_BINARY) != 0;
}
