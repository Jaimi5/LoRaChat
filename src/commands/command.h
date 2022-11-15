#pragma once

#include "Arduino.h"

class Command {
public:
    Command() {};

    //Command with accepting lambda as argument
    Command(String command, String description, std::function<String(String)> callback):
        command(command), description(description), callback(callback) {
    }

    String execute(String args) { return callback(args); };

    String getCommand() {
        return command;
    }

    String getDescription() {
        return description;
    }

private:
    String command;
    String description;
    std::function<String(String)> callback;
};