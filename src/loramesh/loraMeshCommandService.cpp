#include "loraMeshCommandService.h"
#include "loraMeshService.h"

LoRaMeshCommandService::LoRaMeshCommandService() {
    addCommand(
        Command("/getRT", "Get the routing table of the device", Perm::OPEN,
                [this](String args) { return LoRaMeshService::getInstance().getRoutingTable(); }));
}
