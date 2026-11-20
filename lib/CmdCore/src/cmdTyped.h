#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cmdFields.h"
#include "cmdStatus.h"

/**
 * @brief What a typed handler returns: a status and, for an error, an optional number such as
 *        an esp_err_t code. On success the reply is in the fields the handler wrote.
 */
struct TypedResult {
    uint8_t status = 0;
    bool hasDetail = false;
    uint32_t detail = 0;

    TypedResult() = default;
    // NOLINTNEXTLINE(google-explicit-constructor): a generic status is a whole result.
    TypedResult(CommandStatus errorStatus) : status(static_cast<uint8_t>(errorStatus)) {}
    /** An error of the command itself, with a code from 128 to 255. */
    explicit TypedResult(uint8_t errorStatus) : status(errorStatus) {}
    TypedResult(uint8_t errorStatus, uint32_t errorDetail)
        : status(errorStatus), hasDetail(true), detail(errorDetail) {}

    bool ok() const { return status == 0; }
};

/** A command handler that reads typed arguments and writes typed reply fields. */
using TypedHandler = std::function<TypedResult(const Fields& in, Fields& out)>;

/** How the number that comes with an error of a command reads as text. */
enum class ErrorDetailKind : uint8_t {
    /** A decimal number. */
    DECIMAL_NUMBER,
    /** "0x" and the number in lowercase hex, as an esp_err_t code is written. */
    HEX_NUMBER,
    /** The name at the position of the number. */
    NAME_OF,
};

/** The declared detail of one error code of a command. */
struct ErrorDetail {
    uint8_t code = 0;
    ErrorDetailKind kind = ErrorDetailKind::DECIMAL_NUMBER;
    /** Names of a NAME_OF detail, by position. */
    std::vector<std::string> names;
};

/**
 * @brief Reads the detail declaration of error @p code: empty or "u" for a decimal number, "x"
 *        for a hex number, "e(a,b,c)" for a name.
 * @return false, leaving @p out unchanged, for any other text.
 */
bool parseErrorDetail(uint8_t code, const std::string& type, ErrorDetail& out);

/**
 * @return @p detail as the argument of error @p status: by the declaration of that code in
 *         @p details, or in decimal when the code has none or the number names no position.
 */
std::string errorDetailText(const std::vector<ErrorDetail>& details, uint8_t status,
                            uint32_t detail);

/**
 * @brief The typed side of a command: its id on LoRa, the schema of its arguments, the schema
 *        of its reply and the template that turns the reply into text.
 *
 * Ids are written by hand in the declaration and never reused. 0 means the command has no id.
 * Blocks per service: 0x10 node, 0x30 OTA, 0x50 WiFi, 0x60 display, 0x70 mesh and monitor.
 */
struct TypedDeclaration {
    uint8_t id = 0;
    std::string argsText;
    std::string replyText;
    std::string replyTemplate;
    Schema args;
    Schema reply;
    /** The errors whose number is not read as decimal. */
    std::vector<ErrorDetail> details;
};

/**
 * @brief Reads a declaration.
 * @return false, leaving @p out unchanged, if the id is 0, a schema or the template is not
 *         valid, the arguments hold a group, or a text holds '|', ';' or a line break, which
 *         the lines of /cmds cannot carry.
 */
bool parseTypedDeclaration(uint8_t id, const std::string& args, const std::string& reply,
                           const std::string& replyTemplate, TypedDeclaration& out);

/**
 * @brief The text of a typed outcome, as a text command would have given it.
 * @return the rendered template for a success, or the status with the detail as its argument
 *         (errorDetailText()). BUSY if the reply fields do not fit the declaration.
 */
CommandResult typedReplyText(const TypedDeclaration& declaration, const Fields& in,
                             const TypedResult& result, const Fields& out);

/**
 * @brief Runs a typed handler for the arguments of a text line.
 * @return ARG if the arguments do not fit the declaration, otherwise typedReplyText().
 */
CommandResult runTypedFromText(const TypedDeclaration& declaration, const TypedHandler& handler,
                               const std::string& args);

/**
 * @brief Writes what follows the status byte of a binary reply: the reply fields for a success,
 *        the detail as a varint for an error that has one.
 * @return false, leaving @p payload unchanged, if the reply fields do not fit the declaration.
 */
bool encodeTypedReply(const TypedDeclaration& declaration, const TypedResult& result,
                      const Fields& out, std::vector<uint8_t>& payload);

/**
 * @brief Writes what follows the status byte of a binary reply into at most @p maxBytes bytes.
 *
 * A reply whose last field is a group loses rows from its end until it fits. Any other reply
 * is written whole or not at all.
 * @param keptRows gets the number of rows written, when it is not nullptr.
 * @return false, leaving @p payload unchanged, if the reply fields do not fit the declaration
 *         or the reply without rows is longer than @p maxBytes.
 */
bool encodeTypedReplyWithin(const TypedDeclaration& declaration, const TypedResult& result,
                            const Fields& out, size_t maxBytes, std::vector<uint8_t>& payload,
                            size_t* keptRows = nullptr);

/**
 * @brief Reads what follows the status byte of a binary reply.
 * @return false if the bytes do not fit the declaration.
 */
bool decodeTypedReply(const TypedDeclaration& declaration, uint8_t status, const uint8_t* payload,
                      size_t size, TypedResult& result, Fields& out);
