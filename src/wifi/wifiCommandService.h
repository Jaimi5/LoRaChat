#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

class WiFiCommandService : public CommandService {
public:
    WiFiCommandService();
};