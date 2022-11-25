#pragma once

#include "Arduino.h"
#include "command.h"

class CommandService {
public:

    CommandService();

    String executeCommand(String args);

    String executeCommand(uint8_t id, String args = "");

    void addCommand(Command command);

    String helpCommand();

    String publicCommands();

    String back();

    bool hasCommand(String command);

    Command* previousCommand = nullptr;

private:
    uint8_t commandsCount = 0;
    Command* commands;
};