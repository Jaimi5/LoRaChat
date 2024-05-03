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

    String publicCommandsHTML();

    String exit();

    bool hasCommand(String command);

    Command* currentCommand = nullptr;
    Command* previousCommand = nullptr;

    uint16_t getCommandAddress(String command);

private:
    uint8_t commandsCount = 0;
    Command* commands;
};