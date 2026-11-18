#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

class MqttCommandService : public CommandService {
public:
    MqttCommandService();
};