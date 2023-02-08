#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "wifiMessage.h"

class WiFiCommandService: public CommandService {
public:

    WiFiCommandService();
};