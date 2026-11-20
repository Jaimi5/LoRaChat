#pragma once

#include "Arduino.h"

#include <vector>

#include "command.h"

/** The text commands of one service. Command names are unique and compared ignoring case. */
class CommandService {
public:
    virtual ~CommandService() = default;

    /** Adds @p command unless its name or its id is taken. @return false if it was refused. */
    bool addCommand(Command command);

    /** Removes the command named @p name. @return false if there is none. */
    bool removeCommand(const String& name);

    /** @return the command named @p name, or nullptr. */
    const Command* find(const String& name) const;

    /** @return the command with id @p id in a binary frame, or nullptr. Id 0 finds nothing. */
    const Command* findById(uint8_t id) const;

    /** @return one help line per command that may run when it arrives on @p origin. */
    String help(Origin origin) const;

    const std::vector<Command>& commands() const { return commands_; }

private:
    std::vector<Command> commands_;
};
