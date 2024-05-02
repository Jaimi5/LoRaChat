#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "displayMessage.h"

class DisplayCommandService: public CommandService {
public:
    DisplayCommandService();
};