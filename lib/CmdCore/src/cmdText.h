#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cmdAuth.h"

/**
 * @brief A command as typed on the serial console or sent as MQTT text:
 *        "[@<dst hex> ]<line>[ #<counter hex>.<tag hex>]".
 *
 * "@1A2B" sends the line to node 0x1A2B over LoRa; without it the line runs locally. The
 * signature suffix carries the counter (8 hex digits) and the tag (16 hex digits).
 */
struct CommandLine {
    bool remote = false;
    uint16_t dst = 0;
    std::string line;
    bool isSigned = false;
    uint32_t counter = 0;
    CommandTag tag{};
};

/** @return false if the target or the signature is malformed, or the line is empty. */
bool parseCommandLine(const std::string& text, CommandLine& out);

/** @return the signature suffix " #<counter>.<tag>" in lowercase hex. */
std::string formatSignature(uint32_t counter, const CommandTag& tag);

/** Splits "/name args" at the first space; @p args has no surrounding spaces. */
void splitCommand(const std::string& line, std::string& name, std::string& args);

/** A command published to the gateway over MQTT: "<dst hex> <request id> <command line>". */
struct MqttCommand {
    uint16_t dst = 0;
    uint8_t requestId = 0;
    CommandLine command;
};

/** @return false if @p text is not an MQTT command. A target prefix in the line is refused. */
bool parseMqttCommand(const std::string& text, MqttCommand& out);

/** @return @p data as lowercase hex. */
std::string toHex(const uint8_t* data, size_t size);

/** Parses an even number of hex digits. @return false on any other character. */
bool fromHex(const std::string& text, std::vector<uint8_t>& out);
