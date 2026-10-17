#include "commandService.h"

static const char* CMD_TAG = "CommandService";

bool CommandService::addCommand(Command command) {
    if (find(command.getCommand()) != nullptr) {
        ESP_LOGE(CMD_TAG, "Command %s registered twice", command.getCommand().c_str());
        return false;
    }
    commands_.push_back(command);
    return true;
}

Command* CommandService::find(const String& name) {
    for (Command& command : commands_) {
        if (name.equalsIgnoreCase(command.getCommand())) return &command;
    }
    return nullptr;
}

String CommandService::help(Origin origin) const {
    String text;
    for (const Command& command : commands_) {
        Perm perm = command.getPerm();
        if (!commandPermitted(perm, origin, true)) continue;
        text += command.getCommand();
        if (perm == Perm::SIGNED && origin != Origin::SERIAL_CONSOLE) text += " (signed)";
        text += " - " + command.getDescription() + "\n";
    }
    return text;
}
