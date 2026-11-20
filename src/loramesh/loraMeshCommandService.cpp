#include "loraMeshCommandService.h"
#include "loraMeshService.h"

LoRaMeshCommandService::LoRaMeshCommandService() {
    addCommand(Command(0x70, "/getRT", "Get the routing table of the device", Perm::OPEN)
                   .reply("n:u routes:*(dst:h2 via:h2 lq:u hops:u rssi:u? snr:i?/100)",
                          "Routing table, {0} routes[: {1*{0} (hops:{3} lq:{2}[ rssi:-{4}][ "
                          "snr:{5}]) - Via: {1}*, }]")
                   .run([](const Fields&, Fields& out) {
                       return LoRaMeshService::getInstance().routesInto(out);
                   }));
}
