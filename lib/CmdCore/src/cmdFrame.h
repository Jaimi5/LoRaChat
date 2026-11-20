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

/**
 * @brief A command, or its reply, as carried by LoRa in the binary form.
 *
 * Bit 5 of the flags tells the binary form from the text form of CommandFrame.
 * Request: flags (bit 6 signed, bit 5), the command id, the bytes of the argument fields, then
 * for signed frames the counter (u32 LE) and the tag (8 bytes), as in the text form. The tag
 * covers the canonical text line, which the receiver builds from the id and the fields.
 * Reply: flags (bit 7, bit 5, and bit 4 when the command ran before a restart and its reply was
 * lost), the status byte, then the bytes of the reply fields for status 0, or for an error an
 * optional varint with a number such as an esp_err_t code. A reply carries no command id: the
 * request id of the LoRaChat header pairs it with its request.
 *
 * Choices where the design left the detail open:
 * - A request has no length byte. The fields end where the frame ends, or where the signature
 *   starts.
 * - Bit 4 is read in replies only. Other unknown bits are ignored in both directions.
 * - Id 0 is never sent. It stands for a command without an id.
 */
struct BinaryCommandFrame {
    static constexpr uint8_t FLAG_BINARY = 1u << 5;
    static constexpr uint8_t FLAG_REPLAYED = 1u << 4;
    /** Bytes before the fields: flags and command id, or flags and status. */
    static constexpr size_t HEADER_SIZE = 2;

    bool response = false;
    bool isSigned = false;
    /** Reply only: the command ran before a restart and its reply was lost. */
    bool replayed = false;
    /** Command id of a request. Replies carry none. */
    uint8_t id = 0;
    /** Status of a reply. Requests carry none. */
    uint8_t status = 0;
    /** The encoded fields, or the detail of an error. */
    std::vector<uint8_t> payload;
    uint32_t counter = 0;
    CommandTag tag{};

    std::vector<uint8_t> encode() const;

    /**
     * @return false if @p data is not a binary frame (too short, bit 5 clear, signed reply, id
     *         0).
     */
    static bool decode(const uint8_t* data, size_t size, BinaryCommandFrame& out);
};

/** @return true if the flags of the frame in @p data have bit 5 set. */
bool isBinaryCommandFrame(const uint8_t* data, size_t size);
