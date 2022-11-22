#include "loraMeshCommandService.h"
#include "loraMeshService.h"

LoRaMeshCommandService::LoRaMeshCommandService() {
    // addCommand(Command("/sendMessage", "Send a message to another device", LoRaMeshMessageType::sendMessage, 1,
    //     [this](String args) {
    //     return LoRaMeshService::getInstance().sendMessage(args);
    // }));

    addCommand(Command("/getRT", "Get the routing table of the device", LoRaMeshMessageType::getRoutingTable, 1,
        [this](String args) {
        return LoRaMeshService::getInstance().getRoutingTable();
    }));
}
