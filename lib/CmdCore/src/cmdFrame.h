#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cmdAuth.h"

/**
 * @brief A command line, or its reply, as carried by LoRa on CommandApp.
 *
 * Request: flags (bit 7 response, bit 6 signed), the line in UTF-8, then for signed frames the
 * counter (u32 LE) and the tag (8 bytes). Reply: flags, the status byte, then the reply text or
 * the argument of an error, which may be empty. The request id is the messageId of the LoRaChat
 * header that carries the frame.
 */
struct CommandFrame {
    static constexpr uint8_t FLAG_RESPONSE = 1u << 7;
    static constexpr uint8_t FLAG_SIGNED = 1u << 6;
    static constexpr size_t SIGNATURE_SIZE = 4 + sizeof(CommandTag);
    /** Bytes of a reply frame before its text: flags and status. */
    static constexpr size_t REPLY_HEADER_SIZE = 2;

    bool response = false;
    bool isSigned = false;
    /** Status of a reply (CommandStatus or a code of the command). Requests carry none. */
    uint8_t status = 0;
    std::string line;
    uint32_t counter = 0;
    CommandTag tag{};

    std::vector<uint8_t> encode() const;

    /** @return false if @p data is not a frame (too short, signed reply, NUL in the line). */
    static bool decode(const uint8_t* data, size_t size, CommandFrame& out);
};
