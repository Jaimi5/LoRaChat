#include "config.h"

#ifdef CHAT_ENABLED

#include "chatCommandService.h"
#include "chatService.h"

ChatCommandService::ChatCommandService() {
    addCommand(Command("/msg", "Send a text to a node: /msg <address in hex | nickname> <text>",
                       ChatCommand::ChatCmdMsg, 1, [this](String args) {
                           return ChatService::getInstance().msgCommand(args);
                       }));

    addCommand(Command("/chat",
                       "Chat with a node: /chat <address in hex | nickname>, then every line is "
                       "sent to it until /exit",
                       ChatCommand::ChatCmdChat, 1, [this](String args) {
                           return ChatService::getInstance().chatCommand(args);
                       }));

    addCommand(Command("/nodes", "List the reachable nodes and their nicknames",
                       ChatCommand::ChatCmdNodes, 1,
                       [this](String args) { return ChatService::getInstance().nodesCommand(); }));

    addCommand(Command("/name", "Set or show the nickname of this node: /name [nickname]",
                       ChatCommand::ChatCmdName, 1, [this](String args) {
                           return ChatService::getInstance().nameCommand(args);
                       }));
}

#endif  // CHAT_ENABLED
