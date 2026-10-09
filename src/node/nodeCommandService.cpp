#include "nodeCommandService.h"

#include "devices/initDevices.h"
#include "nodeService.h"

NodeCommandService::NodeCommandService() {
    addCommand(Command("/role", "Show the node role (gateway or sensor)", Perm::OPEN,
                       [](String) { return NodeService::getInstance().describeRole(); }));
    addCommand(Command("/role.set", "Set the node role: /role.set gateway|sensor (restarts)",
                       Perm::SIGNED,
                       [](String args) { return NodeService::getInstance().setRole(args); }));
    addCommand(Command("/power", "Show supply, battery and charger state", Perm::OPEN,
                       [](String) { return String(InitDevices::powerReport().c_str()); }));
}
