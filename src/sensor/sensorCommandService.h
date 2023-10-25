#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

class SensorCommandService: public CommandService {
public:
    SensorCommandService();
};