#pragma once

#include "Arduino.h"
#include "command.h"

class CommandService {
public:

    CommandService();

    String executeCommand(String args);

    void addCommand(Command command);

    String helpCommand();

    String back();

private:
    uint8_t commandsCount = 0;
    Command* previousCommand = nullptr;
    Command* commands;
};