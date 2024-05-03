#include "commandService.h"

static const char* CMD_TAG = "CommandService";


CommandService::CommandService() {
    addCommand(Command("/help", "Print help", 0, 0, [this](String args) { return this->helpCommand(); }));
    addCommand(Command("/exit", "Exit", 0, 0, [this](String args) { return this->exit(); }));
}

String CommandService::executeCommand(String args) {
    ESP_LOGV(CMD_TAG, "Executing command: %s", args.c_str());
    if (args.length() == 0) {
        return helpCommand();
    }

    String command = args.substring(0, args.indexOf(" "));
    String commandArgs = "";
    if (args.indexOf(" ") > 0)
        commandArgs = args.substring(args.indexOf(" ") + 1);

    for (uint8_t i = 0; i < commandsCount; i++) {
        if (command.equalsIgnoreCase(commands[i].getCommand())) {
            ESP_LOGV(CMD_TAG, "Executing command: %s", commands[i].getCommand().c_str());
            currentCommand = &commands[i];
            return commands[i].execute(commandArgs);
        }
    }

    if (previousCommand != nullptr)
        return previousCommand->execute(args);

    return String("Command not found\n") + helpCommand();
}

String CommandService::executeCommand(uint8_t id, String args) {
    for (uint8_t i = 0; i < commandsCount; i++) {
        if (commands[i].getCommandID() == id) {
            currentCommand = &commands[i];
            return commands[i].execute(args);
        }
    }

    return "Command not found\n" + helpCommand();
}

void CommandService::addCommand(Command command) {
    Command* newCommands = new Command[commandsCount + 1];

    for (uint8_t i = 0; i < commandsCount; i++) {
        newCommands[i] = commands[i];
    }

    newCommands[commandsCount] = command;

    if (commandsCount > 0) delete[] commands;

    commands = newCommands;

    commandsCount++;
}

String CommandService::exit() {
    String exitString = "";
    if (previousCommand != nullptr)
        exitString = "Exit command: " + previousCommand->getCommand();

    previousCommand = nullptr;
    return exitString;
}

bool CommandService::hasCommand(String command) {
    String firstCommand = command.substring(0, command.indexOf(" "));

    for (uint8_t i = 0; i < commandsCount; i++) {
        if (firstCommand.equalsIgnoreCase(commands[i].getCommand()) || previousCommand != nullptr) {
            return true;
        }
    }

    return false;
}

uint16_t CommandService::getCommandAddress(String command) {
    // If first parameter of the command is a hex number return it, otherwise return 0
    // The HEX value does not have the 0x prefix
    String firstCommand = command.substring(0, command.indexOf(" "));
    if (firstCommand.length() == 0) return 0;

    for (uint8_t i = 0; i < firstCommand.length(); i++) {
        if (!isHexadecimalDigit(firstCommand.charAt(i))) return 0;
    }

    return strtol(firstCommand.c_str(), NULL, 16);
}

String CommandService::helpCommand() {
    String help = "Available commands:\n";
    for (uint8_t i = 0; i < commandsCount; i++) {
        help += commands[i].getCommand() + " (" + commands[i].getCommandID() + ") - " + commands[i].getDescription() + "\n";
    }
    return help;
}

String CommandService::publicCommands() {
    String help = "";
    for (uint8_t i = 0; i < commandsCount; i++) {
        if (commands[i].getPublic())
            help += commands[i].getCommand() + " (" + commands[i].getCommandID() + ") - " + commands[i].getDescription() + "\n";
    }
    return help;
}

String CommandService::publicCommandsHTML() {
    String help = "";
    for (uint8_t i = 0; i < commandsCount; i++) {
        if (commands[i].getPublic())
            help += "<dd>" + commands[i].getCommand() + " - " + commands[i].getDescription() + "</dd>";
    }
    return help;

}
