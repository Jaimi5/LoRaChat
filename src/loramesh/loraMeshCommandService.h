#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "loraMeshMessage.h"

class LoRaMeshCommandService: public CommandService {
public:
    LoRaMeshCommandService();
};