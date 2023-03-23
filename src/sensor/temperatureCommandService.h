#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "sensor.h"

#include "sensorMessage.h"

class TemperatureCommandService: public CommandService {
public:

    TemperatureCommandService();
};