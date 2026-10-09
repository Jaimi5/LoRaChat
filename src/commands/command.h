#pragma once

#include "Arduino.h"

#include <functional>

#include "cmdPerm.h"

/** A text command: its name, help text, permission and handler. */
class Command {
public:
    Command() = default;

    Command(String command, String description, Perm perm,
            std::function<String(String)> callback)
        : command(command),
          description(description),
          callback(callback),
          perm(perm) {}

    String execute(String args) { return callback(args); }

    const String& getCommand() const { return command; }

    const String& getDescription() const { return description; }

    Perm getPerm() const { return perm; }

private:
    String command;
    String description;
    std::function<String(String)> callback;
    Perm perm = Perm::LOCAL_ONLY;
};
