#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Pure helpers for the Chat application: limits, the wire format and the parsing of what the user
// types. No Arduino dependencies, so this header can be compiled and tested on a host.

// Longest nickname, in bytes.
#define CHAT_MAX_NAME 12
// Longest chat text in one message, in bytes.
#define CHAT_MAX_TEXT 160
// Payload header: type + nameLen.
#define CHAT_HEADER_SIZE 2
// Largest chat payload carried in a DataMessage.
#define CHAT_MAX_PAYLOAD (CHAT_HEADER_SIZE + CHAT_MAX_NAME + CHAT_MAX_TEXT)

// Wire format of a chat payload (the bytes after the DataMessage header):
//
//   byte 0        type      ChatMessageType
//   byte 1        nameLen   length of the sender's nickname, 0..CHAT_MAX_NAME (0 = no nickname)
//   nameLen bytes name      sender's nickname, not NUL terminated
//   rest          text      ChatText only: 1..CHAT_MAX_TEXT bytes, not NUL terminated
enum ChatMessageType : uint8_t {
    ChatText = 1,     // A chat line for the user of the destination node
    ChatNameReq = 2,  // Asks the destination for its nickname
    ChatName = 3,     // Answer to ChatNameReq
};

namespace chat {

inline bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline bool isAllHex(const char* s, size_t n) {
    if (n == 0)
        return false;
    for (size_t i = 0; i < n; i++) {
        if (!isHexDigit(s[i]))
            return false;
    }
    return true;
}

/**
 * @brief True when the token is a node address: 1 to 4 hex digits, no prefix.
 * Such a token is always an address, never a nickname.
 */
inline bool parseAddress(const char* s, size_t n, uint16_t& addr) {
    if (n == 0 || n > 4 || !isAllHex(s, n))
        return false;

    uint16_t value = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        uint8_t digit = (c <= '9') ? (c - '0') : ((c | 0x20) - 'a' + 10);
        value = (uint16_t)((value << 4) | digit);
    }
    addr = value;
    return true;
}

/**
 * @brief Nickname rule: 1..CHAT_MAX_NAME characters of [A-Za-z0-9_-], and not something that
 * parseAddress() accepts (so "cafe" or "12" can never shadow a node address).
 */
inline bool isValidName(const char* s, size_t n) {
    if (n == 0 || n > CHAT_MAX_NAME)
        return false;

    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  c == '_' || c == '-';
        if (!ok)
            return false;
    }

    uint16_t unused;
    return !parseAddress(s, n, unused);
}

// ASCII case-insensitive comparison of two nicknames.
inline bool nameEquals(const char* a, size_t an, const char* b, size_t bn) {
    if (an != bn)
        return false;
    for (size_t i = 0; i < an; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z')
            y = (char)(y + 32);
        if (x != y)
            return false;
    }
    return true;
}

/**
 * @brief Split "token rest" on spaces. Leading spaces are skipped; rest starts at the first
 * non-space character after the token and keeps everything up to the end of the string.
 */
inline void splitFirst(const char* s, const char*& token, size_t& tokenLen, const char*& rest) {
    while (*s == ' ')
        s++;
    token = s;
    while (*s != '\0' && *s != ' ')
        s++;
    tokenLen = (size_t)(s - token);
    while (*s == ' ')
        s++;
    rest = s;
}

// Replace control bytes so text from the mesh cannot drive the phone's terminal.
inline void sanitise(char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t c = (uint8_t)s[i];
        if (c < 0x20 || c == 0x7F)
            s[i] = '?';
    }
}

struct Payload {
    uint8_t type = 0;
    const uint8_t* name = nullptr;
    uint8_t nameLen = 0;
    const uint8_t* text = nullptr;
    size_t textLen = 0;
};

/**
 * @brief Validate a received chat payload and locate its fields.
 *
 * @param data Payload bytes
 * @param size Payload size (DataMessage::messageSize)
 * @return false when the payload is malformed and must be dropped
 */
inline bool parsePayload(const uint8_t* data, size_t size, Payload& out) {
    if (data == nullptr || size < CHAT_HEADER_SIZE || size > CHAT_MAX_PAYLOAD)
        return false;

    uint8_t type = data[0];
    uint8_t nameLen = data[1];

    if (type != ChatText && type != ChatNameReq && type != ChatName)
        return false;
    if (nameLen > CHAT_MAX_NAME || (size_t)CHAT_HEADER_SIZE + nameLen > size)
        return false;

    size_t textLen = size - CHAT_HEADER_SIZE - nameLen;
    if (type == ChatText) {
        if (textLen == 0 || textLen > CHAT_MAX_TEXT)
            return false;
    } else if (textLen != 0) {
        return false;
    }

    out.type = type;
    out.nameLen = nameLen;
    out.name = data + CHAT_HEADER_SIZE;
    out.text = data + CHAT_HEADER_SIZE + nameLen;
    out.textLen = textLen;
    return true;
}

/**
 * @brief Write a chat payload into out.
 *
 * @return The payload size, or 0 when the fields do not fit the limits or the buffer.
 */
inline size_t buildPayload(uint8_t* out, size_t cap, uint8_t type, const char* name, size_t nameLen,
                           const char* text, size_t textLen) {
    if (nameLen > CHAT_MAX_NAME || textLen > CHAT_MAX_TEXT)
        return 0;
    if (type == ChatText ? textLen == 0 : textLen != 0)
        return 0;

    size_t size = CHAT_HEADER_SIZE + nameLen + textLen;
    if (out == nullptr || size > cap)
        return 0;

    out[0] = type;
    out[1] = (uint8_t)nameLen;
    if (nameLen > 0)
        memcpy(out + CHAT_HEADER_SIZE, name, nameLen);
    if (textLen > 0)
        memcpy(out + CHAT_HEADER_SIZE + nameLen, text, textLen);
    return size;
}

}  // namespace chat
