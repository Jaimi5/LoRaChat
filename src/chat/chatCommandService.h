#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "chatMessage.h"

// Identifiers of the chat text commands.
enum ChatCommand : uint8_t {
    ChatCmdMsg = 1,
    ChatCmdChat = 2,
    ChatCmdNodes = 3,
    ChatCmdName = 4,
};

class ChatCommandService : public CommandService {
public:
    ChatCommandService();
};
