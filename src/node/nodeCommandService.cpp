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
    addCommand(Command("/identify", "Blink the LEDs to find the board: /identify [seconds] (10)",
                       Perm::OPEN, [](String args) {
                           constexpr long DEFAULT_S = 10;
                           constexpr long MAX_S = 600;
                           long seconds = args.length() > 0 ? args.toInt() : DEFAULT_S;
                           if (seconds <= 0 || seconds > MAX_S) seconds = DEFAULT_S;
                           return String(InitDevices::identify(seconds).c_str());
                       }));
}
