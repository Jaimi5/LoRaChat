#include "commandService.h"

CommandService::CommandService() {
    addCommand(Command("/help", "Print help", [this](String args) { return this->helpCommand(); }));
    addCommand(Command("/back", "Go back", [this](String args) { return this->back(); }));
}

String CommandService::executeCommand(String args) {
    Serial.println("Executing command: " + args);
    if (args.length() == 0) {
        return helpCommand();
    }

    String command = args.substring(0, args.indexOf(" "));
    String commandArgs = args.substring(args.indexOf(" ") + 1);

    for (uint8_t i = 0; i < commandsCount; i++) {
        if (command.indexOf(commands[i].getCommand()) != -1) {
            Serial.println("Executing command: " + commands[i].getCommand());
            return commands[i].execute(commandArgs);
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

String CommandService::back() {
    previousCommand = nullptr;
    return "Going back";
}

String CommandService::helpCommand() {
    String help = "Available commands:\n";
    for (uint8_t i = 0; i < commandsCount; i++) {
        help += commands[i].getCommand() + " - " + commands[i].getDescription() + "\n";
    }
    return help;
}