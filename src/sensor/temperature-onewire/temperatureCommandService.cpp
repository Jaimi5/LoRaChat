#include "temperatureCommandService.h"
#include "temperature.h"

TemperatureCommandService::TemperatureCommandService() {
    addCommand(Command("/getTemperature", "Get the temperature of the device", GetValue, 1,
        [this](String args) {
        return String(Temperature::getInstance().readValue());
    }));
}