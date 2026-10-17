#pragma once

#include "Arduino.h"

#include <vector>

#include "command.h"

/** The text commands of one service. Command names are unique and compared ignoring case. */
class CommandService {
public:
    virtual ~CommandService() = default;

    /** Adds @p command unless its name is already taken. @return false if it was refused. */
    bool addCommand(Command command);

    /** @return the command named @p name, or nullptr. */
    Command* find(const String& name);

    /** @return one help line per command that may run when it arrives on @p origin. */
    String help(Origin origin) const;

    const std::vector<Command>& commands() const { return commands_; }

private:
    std::vector<Command> commands_;
};
