#pragma once

#include <Arduino.h>

#include "config.h"

#include "message/messageService.h"

#include "message/messageManager.h"

#include "chatCommandService.h"

#include "chatMessage.h"

// Nodes whose nickname is remembered. Kept in RAM only.
#define CHAT_MAX_PEERS 16
// Nickname requests sent by one /nodes command.
#define CHAT_NAME_REQUESTS_PER_LIST 5
// Minimum time between two nickname requests to the same node.
#define CHAT_NAME_REQUEST_INTERVAL_MS (10UL * 60UL * 1000UL)

/**
 * @brief Text chat between mesh nodes, driven by commands typed on the Bluetooth serial console.
 *
 * /msg, /chat, /nodes and /name are ordinary commands of this service. Chat mode ("every line goes
 * to that node") is state kept here and applied by the console before command dispatch, so the
 * lines of other commands are never sent over the air.
 *
 * Messages are sent unencrypted and unauthenticated: a nickname is whatever the sender claims, so
 * the address is always shown next to it.
 */
class ChatService : public MessageService {
public:
    static ChatService& getInstance() {
        static ChatService instance;
        return instance;
    }

    ~ChatService() {
        if (chatCommandService != nullptr) {
            delete chatCommandService;
        }
    }

    // Load the stored nickname. Call once from setup(), before the mesh starts.
    void init();

    // Text commands. They return the reply shown to the user.
    String msgCommand(String args);
    String chatCommand(String args);
    String nodesCommand();
    String nameCommand(String args);

    // Chat mode. Only used from the console task.
    bool inChatMode() { return chatPeer != 0; }
    String sendChatLine(const char* line);
    String leaveChat();

    void processReceivedMessage(messagePort port, DataMessage* message);

    ChatCommandService* chatCommandService = nullptr;

private:
    ChatService() : MessageService(appPort::LoRaChat, "Chat") {
        chatCommandService = new ChatCommandService();
        commandService = chatCommandService;
    };

    struct Peer {
        uint16_t addr = 0;  // 0 = free entry
        char name[CHAT_MAX_NAME + 1] = {0};
        uint32_t lastSeenMs = 0;
        uint32_t lastRequestMs = 0;
        bool requested = false;
    };

    // The peer table and ownName are shared by the console task and the mesh receive task.
    SemaphoreHandle_t mutex = NULL;
    Peer peers[CHAT_MAX_PEERS];
    char ownName[CHAT_MAX_NAME + 1] = {0};

    uint16_t chatPeer = 0;
    uint8_t nextMessageId = 0;

    bool lock();
    void unlock();
    Peer* findPeerLocked(uint16_t addr);
    Peer* touchPeerLocked(uint16_t addr);

    void learnName(uint16_t addr, const uint8_t* name, uint8_t nameLen);
    void getLabel(uint16_t addr, char* out, size_t size);
    bool resolveNode(const char* token, size_t tokenLen, uint16_t& addr, String& error);
    bool sendChat(ChatMessageType type, uint16_t dst, const char* text, size_t textLen);
    String sendText(uint16_t dst, const char* text, bool echo);
    void deliver(const char* line);
};
