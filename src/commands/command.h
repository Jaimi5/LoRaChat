#pragma once

#include "Arduino.h"

#include <functional>

#include "cmdPerm.h"

/** A text command: its name, help text, permission and handler. */
class Command {
public:
    Command() = default;

    Command(String command, String description, uint8_t commandId, Perm perm,
            std::function<String(String)> callback)
        : command(command),
          description(description),
          callback(callback),
          commandId(commandId),
          perm(perm) {}

    String execute(String args) { return callback(args); }

    const String& getCommand() const { return command; }

    const String& getDescription() const { return description; }

    uint8_t getCommandID() const { return commandId; }

    Perm getPerm() const { return perm; }

private:
    String command;
    String description;
    std::function<String(String)> callback;
    uint8_t commandId = 0;
    Perm perm = Perm::LOCAL_ONLY;
};
