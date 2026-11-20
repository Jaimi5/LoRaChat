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
 * @return the rendered template for a success, or the status with the detail in decimal as its
 *         argument. BUSY if the reply fields do not fit the declaration.
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
 * @brief Reads what follows the status byte of a binary reply.
 * @return false if the bytes do not fit the declaration.
 */
bool decodeTypedReply(const TypedDeclaration& declaration, uint8_t status, const uint8_t* payload,
                      size_t size, TypedResult& result, Fields& out);
