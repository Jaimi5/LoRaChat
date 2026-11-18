#include "config.h"

#ifdef CHAT_ENABLED

#include "chatService.h"

#include <new>

#include <Preferences.h>

#include "bluetooth/bluetoothService.h"

#include "loramesh/loraMeshService.h"

static const char* CHAT_TAG = "ChatService";

static const char* CHAT_PREFS_NAMESPACE = "chat";
static const char* CHAT_PREFS_NAME_KEY = "nick";

// "nickname(1A2B)" plus the terminator.
static const size_t CHAT_LABEL_SIZE = CHAT_MAX_NAME + 8;

void ChatService::init() {
    if (mutex == NULL)
        mutex = xSemaphoreCreateMutex();

    Preferences prefs;
    if (prefs.begin(CHAT_PREFS_NAMESPACE, true)) {
        String stored = prefs.getString(CHAT_PREFS_NAME_KEY, "");
        prefs.end();

        if (chat::isValidName(stored.c_str(), stored.length()) && lock()) {
            memcpy(ownName, stored.c_str(), stored.length());
            ownName[stored.length()] = '\0';
            unlock();
        }
    }

    ESP_LOGI(CHAT_TAG, "Chat initialized, nickname: %s", ownName[0] ? ownName : "(none)");
}

bool ChatService::lock() {
    return mutex != NULL && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE;
}

void ChatService::unlock() {
    xSemaphoreGive(mutex);
}

ChatService::Peer* ChatService::findPeerLocked(uint16_t addr) {
    for (Peer& peer : peers) {
        if (peer.addr == addr)
            return &peer;
    }
    return nullptr;
}

ChatService::Peer* ChatService::touchPeerLocked(uint16_t addr) {
    Peer* peer = findPeerLocked(addr);

    if (peer == nullptr) {
        // Take a free entry; otherwise replace the entry seen longest ago, preferring the ones
        // that never told us a nickname.
        Peer* victim = &peers[0];
        for (Peer& candidate : peers) {
            if (candidate.addr == 0) {
                victim = &candidate;
                break;
            }

            bool candidateNamed = candidate.name[0] != '\0';
            bool victimNamed = victim->name[0] != '\0';
            if (candidateNamed != victimNamed) {
                if (!candidateNamed)
                    victim = &candidate;
            } else if ((int32_t)(candidate.lastSeenMs - victim->lastSeenMs) < 0) {
                victim = &candidate;
            }
        }

        *victim = Peer();
        victim->addr = addr;
        peer = victim;
    }

    peer->lastSeenMs = millis();
    return peer;
}

void ChatService::learnName(uint16_t addr, const uint8_t* name, uint8_t nameLen) {
    // A name that breaks the nickname rule is ignored, so a peer cannot pick a name that looks
    // like an address or that carries control characters.
    bool valid = chat::isValidName((const char*)name, nameLen);
    if (nameLen != 0 && !valid)
        return;

    if (!lock())
        return;

    Peer* peer = touchPeerLocked(addr);
    memcpy(peer->name, name, nameLen);
    peer->name[nameLen] = '\0';

    unlock();
}

void ChatService::getLabel(uint16_t addr, char* out, size_t size) {
    char name[CHAT_MAX_NAME + 1] = {0};

    if (lock()) {
        Peer* peer = findPeerLocked(addr);
        if (peer != nullptr)
            memcpy(name, peer->name, sizeof(name));
        unlock();
    }

    if (name[0] != '\0')
        snprintf(out, size, "%s(%04X)", name, addr);
    else
        snprintf(out, size, "%04X", addr);
}

bool ChatService::resolveNode(const char* token, size_t tokenLen, uint16_t& addr, String& error) {
    uint16_t found = 0;

    if (chat::parseAddress(token, tokenLen, found)) {
        if (found == 0 || found == 0xFFFF) {
            error = "Invalid address";
            return false;
        }
    } else if (chat::isValidName(token, tokenLen)) {
        uint8_t matches = 0;
        String candidates = "";

        if (lock()) {
            for (Peer& peer : peers) {
                if (peer.addr != 0 &&
                    chat::nameEquals(token, tokenLen, peer.name, strlen(peer.name))) {
                    matches++;
                    found = peer.addr;
                    char hex[6];
                    snprintf(hex, sizeof(hex), " %04X", peer.addr);
                    candidates += hex;
                }
            }
            unlock();
        }

        if (matches == 0) {
            error = "Unknown nickname. Use /nodes to list the nodes, or the address in hex";
            return false;
        }
        if (matches > 1) {
            error = "Several nodes use that nickname, use the address:" + candidates;
            return false;
        }
    } else {
        error = "Not a node: use an address in hex (1-4 digits) or a nickname";
        return false;
    }

    if (found == LoRaMeshService::getInstance().getLocalAddress()) {
        error = "That is this node";
        return false;
    }

    bool hasRoute = false;
    for (uint16_t node : LoRaMeshService::getInstance().getNodeAddresses()) {
        if (node == found) {
            hasRoute = true;
            break;
        }
    }

    if (!hasRoute) {
        char label[CHAT_LABEL_SIZE];
        getLabel(found, label, sizeof(label));
        error = String("No route to ") + label + ". Use /nodes to list the reachable nodes";
        return false;
    }

    addr = found;
    return true;
}

bool ChatService::sendChat(ChatMessageType type, uint16_t dst, const char* text, size_t textLen) {
    char name[CHAT_MAX_NAME + 1] = {0};
    if (lock()) {
        memcpy(name, ownName, sizeof(name));
        unlock();
    }

    // Zero-initialized, so the header fields that are not set below are 0.
    uint8_t* buffer = new (std::nothrow) uint8_t[sizeof(DataMessage) + CHAT_MAX_PAYLOAD]();
    if (buffer == nullptr) {
        ESP_LOGE(CHAT_TAG, "No memory for a chat message");
        return false;
    }

    DataMessage* message = (DataMessage*)buffer;

    size_t size = chat::buildPayload(message->message, CHAT_MAX_PAYLOAD, type, name, strlen(name),
                                     text, textLen);
    if (size == 0) {
        delete[] buffer;
        return false;
    }

    message->appPortDst = appPort::LoRaChat;
    message->appPortSrc = appPort::LoRaChat;
    message->messageId = nextMessageId++;
    message->addrSrc = LoRaMeshService::getInstance().getLocalAddress();
    message->addrDst = dst;
    message->messageSize = size;

    MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, message);

    delete[] buffer;
    return true;
}

String ChatService::sendText(uint16_t dst, const char* text, bool echo) {
    size_t textLen = strlen(text);
    if (textLen == 0)
        return "Nothing to send";
    if (textLen > CHAT_MAX_TEXT)
        return "Text too long (max " + String(CHAT_MAX_TEXT) + " bytes), not sent";

    if (!sendChat(ChatMessageType::ChatText, dst, text, textLen))
        return "Could not send the message";

    if (!echo)
        return "";

    // The mesh gives no delivery confirmation, so this only says the message was handed over.
    char label[CHAT_LABEL_SIZE];
    getLabel(dst, label, sizeof(label));
    return String("-> ") + label + ": " + text;
}

String ChatService::msgCommand(String args) {
    const char* token;
    const char* text;
    size_t tokenLen;
    chat::splitFirst(args.c_str(), token, tokenLen, text);

    if (tokenLen == 0 || *text == '\0')
        return "Usage: /msg <address in hex | nickname> <text>";

    uint16_t dst = 0;
    String error;
    if (!resolveNode(token, tokenLen, dst, error))
        return error;

    return sendText(dst, text, true);
}

String ChatService::chatCommand(String args) {
    const char* token;
    const char* rest;
    size_t tokenLen;
    chat::splitFirst(args.c_str(), token, tokenLen, rest);

    char label[CHAT_LABEL_SIZE];

    if (tokenLen == 0) {
        if (!inChatMode())
            return "Usage: /chat <address in hex | nickname>";

        getLabel(chatPeer, label, sizeof(label));
        return String("Chatting with ") + label + ". /exit to leave";
    }

    if (*rest != '\0')
        return "Usage: /chat <address in hex | nickname>. To send one text use /msg";

    uint16_t dst = 0;
    String error;
    if (!resolveNode(token, tokenLen, dst, error))
        return error;

    chatPeer = dst;

    getLabel(chatPeer, label, sizeof(label));
    return String("Chatting with ") + label + ". Every line is sent to it. /exit to leave";
}

String ChatService::sendChatLine(const char* line) {
    if (!inChatMode())
        return "";

    // The terminal already shows what the user typed, so only errors are reported.
    return sendText(chatPeer, line, false);
}

String ChatService::leaveChat() {
    if (!inChatMode())
        return "";

    chatPeer = 0;
    return "Left chat";
}

String ChatService::nodesCommand() {
    LoRaMeshService& mesh = LoRaMeshService::getInstance();

    char line[CHAT_MAX_NAME + 24];
    char name[CHAT_MAX_NAME + 1] = {0};

    if (lock()) {
        memcpy(name, ownName, sizeof(name));
        unlock();
    }

    snprintf(line, sizeof(line), "This node: %04X %s\n", mesh.getLocalAddress(),
             name[0] ? name : "-");
    String result = line;

    std::vector<uint16_t> nodes = mesh.getNodeAddresses();
    if (nodes.empty())
        return result + "No other nodes reachable yet";

    uint16_t askNames[CHAT_NAME_REQUESTS_PER_LIST];
    uint8_t askCount = 0;
    uint32_t now = millis();

    for (uint16_t node : nodes) {
        name[0] = '\0';

        if (lock()) {
            Peer* peer = findPeerLocked(node);
            if (peer != nullptr)
                memcpy(name, peer->name, sizeof(name));

            // Ask the nodes without a known nickname for it, a few per call and not too often.
            if (name[0] == '\0' && askCount < CHAT_NAME_REQUESTS_PER_LIST) {
                bool canAsk = peer == nullptr || !peer->requested ||
                              now - peer->lastRequestMs >= CHAT_NAME_REQUEST_INTERVAL_MS;
                if (canAsk) {
                    peer = touchPeerLocked(node);
                    peer->requested = true;
                    peer->lastRequestMs = now;
                    askNames[askCount++] = node;
                }
            }
            unlock();
        }

        snprintf(line, sizeof(line), "%04X %s\n", node, name[0] ? name : "-");
        result += line;
    }

    for (uint8_t i = 0; i < askCount; i++)
        sendChat(ChatMessageType::ChatNameReq, askNames[i], nullptr, 0);

    return result;
}

String ChatService::nameCommand(String args) {
    args.trim();

    if (args.length() == 0) {
        char name[CHAT_MAX_NAME + 1] = {0};
        if (lock()) {
            memcpy(name, ownName, sizeof(name));
            unlock();
        }
        return name[0] ? String("Name: ") + name : String("No name set. Usage: /name <nickname>");
    }

    if (!chat::isValidName(args.c_str(), args.length()))
        return "Invalid name: use 1-" + String(CHAT_MAX_NAME) +
               " characters of A-Z a-z 0-9 _ -, and not a 1-4 digit hex number";

    bool changed = false;
    if (lock()) {
        changed = strcmp(ownName, args.c_str()) != 0;
        memcpy(ownName, args.c_str(), args.length());
        ownName[args.length()] = '\0';
        unlock();
    }

    if (changed) {
        Preferences prefs;
        if (!prefs.begin(CHAT_PREFS_NAMESPACE, false) ||
            prefs.putString(CHAT_PREFS_NAME_KEY, args) == 0) {
            ESP_LOGE(CHAT_TAG, "Could not store the nickname");
        }
        prefs.end();
    }

    return "Name set to " + args;
}

void ChatService::deliver(const char* line) {
    ESP_LOGI(CHAT_TAG, "%s", line);

    // Shown only when a phone is connected; received messages are not stored.
    BluetoothService::getInstance().writeLine(line);
}

void ChatService::processReceivedMessage(messagePort port, DataMessage* message) {
    // Runs in the mesh receive task and the message is freed when this returns, so everything
    // that is kept is copied, with lengths taken from the validated payload.
    chat::Payload payload;
    if (!chat::parsePayload(message->message, message->messageSize, payload)) {
        ESP_LOGW(CHAT_TAG, "Malformed chat message from %04X dropped", message->addrSrc);
        return;
    }

    uint16_t src = message->addrSrc;
    if (src == 0 || src == 0xFFFF || src == LoRaMeshService::getInstance().getLocalAddress())
        return;

    learnName(src, payload.name, payload.nameLen);

    switch (payload.type) {
        case ChatMessageType::ChatText: {
            char label[CHAT_LABEL_SIZE];
            getLabel(src, label, sizeof(label));

            char text[CHAT_MAX_TEXT + 1];
            memcpy(text, payload.text, payload.textLen);
            text[payload.textLen] = '\0';
            chat::sanitise(text, payload.textLen);

            char line[CHAT_LABEL_SIZE + 2 + CHAT_MAX_TEXT + 1];
            snprintf(line, sizeof(line), "%s: %s", label, text);
            deliver(line);
            break;
        }
        case ChatMessageType::ChatNameReq:
            sendChat(ChatMessageType::ChatName, src, nullptr, 0);
            break;
        case ChatMessageType::ChatName:
            // The nickname has been stored above.
            break;
        default:
            break;
    }
}

#endif  // CHAT_ENABLED
