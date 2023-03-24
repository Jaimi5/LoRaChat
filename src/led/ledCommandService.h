#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "ledMessage.h"

class LedCommandService: public CommandService {
public:
    LedCommandService();
};