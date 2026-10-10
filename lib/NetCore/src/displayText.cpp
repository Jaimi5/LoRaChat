#include "displayText.h"

#include <cstring>

std::string displayTextFrom(const char* text, size_t size) {
    if (text == nullptr) return {};
    const void* nul = std::memchr(text, '\0', size);
    return std::string(text, nul ? static_cast<const char*>(nul) - text : size);
}

size_t displayTextLimit(size_t maxPayload, size_t capacity) {
    if (maxPayload < 2 || capacity < 1) return 0;
    size_t fromPayload = maxPayload - 2;
    size_t fromBuffer = capacity - 1;
    return fromPayload < fromBuffer ? fromPayload : fromBuffer;
}
