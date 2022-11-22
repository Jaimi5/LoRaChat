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
    String commandArgs = args.substring(args.indexOf(" ") + 1);

    for (uint8_t i = 0; i < commandsCount; i++) {
        if (command.indexOf(commands[i].getCommand()) != -1) {
            Serial.println("Executing command: " + commands[i].getCommand());
            return commands[i].execute(commandArgs);
        }
    }

    return "Command not found\n" + helpCommand();
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
    for (uint8_t i = 0; i < commandsCount; i++) {
        if (commands[i].getCommand().indexOf(command) != -1) {
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
