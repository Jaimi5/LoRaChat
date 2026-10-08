#pragma once

#include <cstdint>
#include <string>

/** What a node does in the network. Values are stored in NVS, never renumber them. */
enum class NodeRole : uint8_t {
    /** Mesh node; WiFi only in maintenance windows. */
    SENSOR = 0,
    /** LoRaMesher network manager with WiFi and MQTT always on. */
    GATEWAY = 1,
};

/** @return "sensor" or "gateway". */
const char* nodeRoleName(NodeRole role);

/** Parses "sensor" or "gateway" (any case). @return false for anything else. */
bool parseNodeRole(const std::string& text, NodeRole& out);

/** Decodes a stored role value. @return false if @p value is not a role. */
bool nodeRoleFromValue(uint8_t value, NodeRole& out);

/**
 * @return the role of a node that has none stored: the node at @p gatewayAddress is the gateway,
 *         every other node is a sensor.
 */
NodeRole defaultNodeRole(uint16_t address, uint16_t gatewayAddress);
