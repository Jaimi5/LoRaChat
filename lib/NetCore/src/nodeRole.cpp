#include "nodeRole.h"

const char* nodeRoleName(NodeRole role) {
    return role == NodeRole::GATEWAY ? "gateway" : "sensor";
}

bool nodeRoleFromValue(uint8_t value, NodeRole& out) {
    if (value > static_cast<uint8_t>(NodeRole::GATEWAY)) return false;
    out = static_cast<NodeRole>(value);
    return true;
}

NodeRole defaultNodeRole(uint16_t address, uint16_t gatewayAddress) {
    return address == gatewayAddress ? NodeRole::GATEWAY : NodeRole::SENSOR;
}
