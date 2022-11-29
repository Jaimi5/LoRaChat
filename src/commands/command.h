#pragma once

#include "Arduino.h"

class Command {
public:
    Command() {};

    //Command with accepting lambda as argument
    Command(const __FlashStringHelper*, const __FlashStringHelper*, uint8_t commandId, bool isPublic, std::function<String(String)> callback):
        command(command), description(description), commandId(commandId), isPublic(isPublic), callback(callback) {
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

    String toString() {
        return String(command) + " (" + String(commandId) + ") - " + String(description);
    }

private:
    const __FlashStringHelper* command;
    const __FlashStringHelper* description;
    bool isPublic;
    uint8_t commandId;
    std::function<String(String)> callback;
};