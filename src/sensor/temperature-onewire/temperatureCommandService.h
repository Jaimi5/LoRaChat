#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "sensor/sensor.h"

#include "sensor/sensorMessage.h"

class TemperatureCommandService: public CommandService {
public:

    TemperatureCommandService();
};