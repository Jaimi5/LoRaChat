#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "loraChatMessage.h"

class LoRaChatCommandService: public CommandService {
public:

    LoRaChatCommandService();
};