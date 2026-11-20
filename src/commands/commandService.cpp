#include "commandService.h"

static const char* CMD_TAG = "CommandService";

bool CommandService::addCommand(Command command) {
    if (find(command.getCommand()) != nullptr) {
        ESP_LOGE(CMD_TAG, "Command %s registered twice", command.getCommand().c_str());
        return false;
    }
    if (command.getId() != 0 && findById(command.getId()) != nullptr) {
        ESP_LOGE(CMD_TAG, "Command %s has id 0x%02X, which is taken", command.getCommand().c_str(),
                 command.getId());
        return false;
    }
    commands_.push_back(command);
    return true;
}

bool CommandService::removeCommand(const String& name) {
    for (auto it = commands_.begin(); it != commands_.end(); ++it) {
        if (!name.equalsIgnoreCase(it->getCommand())) continue;
        commands_.erase(it);
        return true;
    }
    return false;
}

const Command* CommandService::find(const String& name) const {
    for (const Command& command : commands_) {
        if (name.equalsIgnoreCase(command.getCommand())) return &command;
    }
    return nullptr;
}

const Command* CommandService::findById(uint8_t id) const {
    if (id == 0) return nullptr;
    for (const Command& command : commands_) {
        if (command.getId() == id) return &command;
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
        if (command.getUsage().length() > 0) text += " " + command.getUsage();
        text += " - ";
        text += command.getDescription();
        text += "\n";
    }
    return text;
}
