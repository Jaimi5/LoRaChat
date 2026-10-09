#include "nodeCommandService.h"

#include "devices/initDevices.h"
#include "nodeService.h"

namespace {
constexpr uint8_t COMMAND_ROLE = 1;
constexpr uint8_t COMMAND_ROLE_SET = 2;
constexpr uint8_t COMMAND_POWER = 3;
}  // namespace

NodeCommandService::NodeCommandService() {
    addCommand(Command("/role", "Show the node role (gateway or sensor)", COMMAND_ROLE, Perm::OPEN,
                       [](String) { return NodeService::getInstance().describeRole(); }));
    addCommand(Command("/role.set", "Set the node role: /role.set gateway|sensor (restarts)",
                       COMMAND_ROLE_SET, Perm::SIGNED,
                       [](String args) { return NodeService::getInstance().setRole(args); }));
    addCommand(Command("/power", "Show supply, battery and charger state", COMMAND_POWER,
                       Perm::OPEN, [](String) { return String(InitDevices::powerReport().c_str()); }));
}
