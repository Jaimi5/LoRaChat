#include "commandService.h"

CommandService::CommandService() {
    addCommand(Command("/help", "Print help", 0, 0, [this](String args) { return this->helpCommand(); }));
    addCommand(Command("/back", "Go back", 0, 0, [this](String args) { return this->back(); }));
}

String CommandService::executeCommand(String args) {
    Serial.println("Executing command: " + args);
    if (args.length() == 0) {
        return helpCommand();
    }

    String command = args.substring(0, args.indexOf(" "));
    String commandArgs = "";
    if (args.indexOf(" ") > 0)
        commandArgs = args.substring(args.indexOf(" ") + 1);

    if (previousCommand != nullptr)
        return previousCommand->execute(args);

    for (uint8_t i = 0; i < commandsCount; i++) {
        if (command.equalsIgnoreCase(commands[i].getCommand())) {
            Serial.println("Executing command: " + commands[i].getCommand());
            return commands[i].execute(commandArgs);
        }
    }

    return String("Command not found\n") + helpCommand();
}

String CommandService::executeCommand(uint8_t id, String args) {
    for (uint8_t i = 0; i < commandsCount; i++) {
        if (commands[i].getCommandID() == id) {
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

String CommandService::back() {
    previousCommand = nullptr;
    return "Going back";
}

bool CommandService::hasCommand(String command) {
    String firstCommand = command.substring(0, command.indexOf(" "));

    for (uint8_t i = 0; i < commandsCount; i++) {
        if (firstCommand.equalsIgnoreCase(commands[i].getCommand())) {
            return true;
        }
    }

    return false;
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
