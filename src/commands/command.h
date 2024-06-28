#pragma once

#include "Arduino.h"

class Command {
private:
    String command;
    String description;
    bool isPublic;
    std::function<String(String)> callback;
    uint8_t commandId;

public:
    Command() {};

    //Command with accepting lambda as argument
    Command(String command, String description, uint8_t commandId, bool isPublic, std::function<String(String)> callback):
        command(command), description(description), isPublic(isPublic), callback(callback), commandId(commandId) {
    }

    String execute(String args) { return callback(args); };

    String getCommand() {
        return command;
    }

    String getDescription() {
        return description;
    }

    uint8_t getCommandID() {
        return commandId;
    }

    bool getPublic() {
        return isPublic;
    }
};