#include "nodeRole.h"

#include <algorithm>
#include <cctype>

const char* nodeRoleName(NodeRole role) {
    return role == NodeRole::GATEWAY ? "gateway" : "sensor";
}

bool parseNodeRole(const std::string& text, NodeRole& out) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "gateway") {
        out = NodeRole::GATEWAY;
        return true;
    }
    if (lower == "sensor") {
        out = NodeRole::SENSOR;
        return true;
    }
    return false;
}

bool nodeRoleFromValue(uint8_t value, NodeRole& out) {
    if (value > static_cast<uint8_t>(NodeRole::GATEWAY)) return false;
    out = static_cast<NodeRole>(value);
    return true;
}

NodeRole defaultNodeRole(uint16_t address, uint16_t gatewayAddress) {
    return address == gatewayAddress ? NodeRole::GATEWAY : NodeRole::SENSOR;
}
