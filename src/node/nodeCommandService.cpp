#include "nodeCommandService.h"

#include "devices/initDevices.h"
#include "loramesh/loraMeshService.h"
#include "nodeService.h"

NodeCommandService::NodeCommandService() {
    addCommand(Command("/role", "Show the node role (gateway or sensor)", Perm::OPEN,
                       [](String) { return NodeService::getInstance().describeRole(); }));
    addCommand(Command("/role.set", "Set the node role: /role.set gateway|sensor (restarts)",
                       Perm::SIGNED,
                       [](String args) { return NodeService::getInstance().setRole(args); }));
    addCommand(Command("/sleep", "Show the sleep settings and the sleeps since power-on",
                       Perm::OPEN, [](String) {
                           return NodeService::getInstance().describeSleep() + "; " +
                                  LoRaMeshService::getInstance().sleepReport().c_str();
                       }));
    addCommand(Command("/sleep.set", "Set the sensor sleep mode: /sleep.set none|light|deep "
                                     "(restarts)",
                       Perm::SIGNED,
                       [](String args) { return NodeService::getInstance().setSleepMode(args); }));
    addCommand(Command("/sleep.fraction",
                       "Set the minimum sleep share of the superframe on the network manager: "
                       "/sleep.fraction <0-90> (restarts)",
                       Perm::SIGNED, [](String args) {
                           return NodeService::getInstance().setSleepFraction(args);
                       }));
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
