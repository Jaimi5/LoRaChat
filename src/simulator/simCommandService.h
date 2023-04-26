#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "simMessage.h"

class SimCommandService: public CommandService {
public:
    SimCommandService();
};