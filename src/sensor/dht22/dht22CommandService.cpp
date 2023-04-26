#include "dht22CommandService.h"
#include "dht22.h"

Dht22CommandService::Dht22CommandService() {
    addCommand(Command("/getTemperature", "Get the temperature of the device", GetValue, 1,
        [this](String args) {
        return String(Dht22::getInstance().readValue());
    }));
}