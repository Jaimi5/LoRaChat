#include "loraChatService.h"

void LoRaChatService::processReceivedMessage(messagePort port, DataMessage* message) {
    LoRaChatMessage* request = (LoRaChatMessage*) message;
    switch (request->type) {
        case LoRaChatMessageType::requestContactInfo:
            responseContactInfo(port, request);
            break;

        case LoRaChatMessageType::responseContactInfo:
            addContact(message);
            break;

        case LoRaChatMessageType::requestGPS:
            responseGPS(port, message);
            break;

        case LoRaChatMessageType::chatTo:
            receiveChatMessage(port, message);
            break;

        case LoRaChatMessageType::ackChat:
            receivedChatMessage(message);
            break;

        default:
            break;
    }
}

void LoRaChatService::initLoRaChatService() {

}

void LoRaChatService::addContact(DataMessage* message) {
    LoRaChatMessageInfo* contactMessage = (LoRaChatMessageInfo*) message;
    addContact(String(contactMessage->name, MAX_NAME_LENGTH), message->addrSrc);
}

void LoRaChatService::addContact(String name, uint16_t src) {
    contactsList->setInUse();
    if (contactsList->moveToStart()) {
        do {
            Info* ci = contactsList->getCurrent();
            if (ci) {
                if (ci->address == src) {
                    name.toCharArray(ci->name, MAX_NAME_LENGTH);
                    contactsList->releaseInUse();
                    BluetoothService::getInstance().writeToBluetooth(String("Changed name: ") + name + String(" - ") + String(src));
                    return;
                }
            }

        } while (contactsList->next());
    }

    Info* ci = new Info();
    ci->address = src;
    name.toCharArray(ci->name, MAX_NAME_LENGTH);

    contactsList->Append(ci);
    contactsList->releaseInUse();

    BluetoothService::getInstance().writeToBluetooth(String("New contact: ") + name + String(" - ") + String(src));
}

String LoRaChatService::getNameContact(uint16_t addr) {
    contactsList->setInUse();
    if (contactsList->moveToStart()) {
        do {
            Info* ci = contactsList->getCurrent();
            if (ci) {
                if (ci->address == addr) {
                    contactsList->releaseInUse();
                    return String(ci->name);
                }
            }

        } while (contactsList->next());
    }

    contactsList->releaseInUse();
    return String();
}

uint16_t LoRaChatService::getAddrContact(String name) {
    contactsList->setInUse();
    if (contactsList->moveToStart()) {
        do {
            Info* ci = contactsList->getCurrent();
            if (ci) {
                if (String(ci->name) == name) {
                    contactsList->releaseInUse();
                    return ci->address;
                }
            }

        } while (contactsList->next());
    }

    contactsList->releaseInUse();
    return 0;
}

String LoRaChatService::getContactsString() {
    String contacts = "--- List of contacts ---";
    contactsList->setInUse();
    if (contactsList->moveToStart()) {
        do {
            Info* contact = contactsList->getCurrent();
            if (contact) {
                contacts += F(CR " - ");
                contacts += (String) contact->address + ": " + contact->name;
            }
        } while (contactsList->next());
    }
    else {
        contacts += F(CR "Empty List");
    }
    contactsList->releaseInUse();

    contacts += F(CR);
    return contacts;
}

String LoRaChatService::changeName(String newName) {
    if (newName.length() > MAX_NAME_LENGTH) {
        return String("Name too long");
    }
    else if (newName.length() == 0) {
        return String("No name added");
    }
    else {
        newName.toCharArray(myName, MAX_NAME_LENGTH);
        return String("Name changed to: " + newName);
    }
}

String LoRaChatService::requestGPS(messagePort port, String name) {
    uint16_t addr = getAddrContact(name);
    if (addr == 0) {
        return String("No contact found");
    }

    LoRaChatMessage* msg = createLoRaChatMessage();
    msg->addrDst = addr;
    msg->type = LoRaChatMessageType::requestGPS;
    msg->appPortDst = appPort::LoRaChat;

    MessageManager::getInstance().sendMessage(port, (DataMessage*) msg);

    delete msg;

    return "Request GPS sent, waiting for response";
}

String LoRaChatService::responseGPS(messagePort port, DataMessage* message) {
    message->appPortDst = appPort::GPSApp;
    message->type = GPSMessageType::reqGPS;
    GPSService::getInstance().processReceivedMessage(port, message);

    return "GPS requested";
}

String LoRaChatService::startChatTo(String name) {
    uint16_t addr = getAddrContact(name);
    if (addr == 0)
        return String("No contact found") + "\n" + getContactsString();

    String startingChat = "Starting chat with: " + name + "\n";
    startingChat += "Type '/exit' to exit chat\n";

    chatAddr = addr;

    loraChatCommandService->previousCommand = loraChatCommandService->currentCommand;

    return startingChat;
}

String LoRaChatService::chatTo(String args) {
    if (loraChatCommandService->previousCommand == nullptr)
        return startChatTo(args);

    LoRaChatMessage* msg = createLoRaChatMessage(args);
    msg->addrDst = chatAddr;
    msg->type = LoRaChatMessageType::chatTo;
    msg->appPortDst = appPort::LoRaChat;

    MessageManager::getInstance().sendMessage(messagePort::LoRaMeshPort, (DataMessage*) msg);

    free(msg);

    return String("Message sent");
}

String LoRaChatService::receiveChatMessage(messagePort port, DataMessage* message) {
    ackChatMessage(port, message);

    LoRaChatMessage* msg = (LoRaChatMessage*) message;
    String name = getNameContact(msg->addrSrc);
    if (name.length() == 0)
        name = String(msg->addrSrc);

    uint32_t msgSize = msg->messageSize - sizeof(LoRaChatMessage);

    String chatMessage = name + ": " + String(msg->message, msgSize) + "\n";

    BluetoothService::getInstance().writeToBluetooth(chatMessage);

    return "Message received";
}

String LoRaChatService::ackChatMessage(messagePort port, DataMessage* message) {
    LoRaChatMessage* msg = createLoRaChatMessage();
    msg->type = LoRaChatMessageType::ackChat;
    msg->appPortDst = appPort::LoRaChat;
    msg->addrDst = message->addrSrc;

    MessageManager::getInstance().sendMessage(port, (DataMessage*) msg);

    delete msg;

    return String("Message sent");
}

String LoRaChatService::receivedChatMessage(DataMessage* message) {
    LoRaChatMessage* msg = (LoRaChatMessage*) message;
    String name = getNameContact(msg->addrSrc);
    if (name.length() == 0)
        name = String(msg->addrSrc);

    String chatMessage = "Message received to " + name + "\n";

    BluetoothService::getInstance().writeToBluetooth(chatMessage);

    return "Message received";
}

String LoRaChatService::findContacts() {
    LM_LinkedList<RouteNode>* nodes = LoraMesher::getInstance().routingTableList();

    bool sent = false;

    nodes->setInUse();
    if (nodes->moveToStart()) {
        do {
            RouteNode* node = nodes->getCurrent();
            if (node) {
                //TODO: This could be a loop to all message ports
                requestContactInfo(messagePort::LoRaMeshPort, node->networkNode.address);
                sent = true;
            }
        } while (nodes->next());
    }

    nodes->releaseInUse();

    return sent ? "Request sent, waiting for response" : "No contacts found";
}

void LoRaChatService::requestContactInfo(messagePort port, uint16_t dst) {
    LoRaChatMessage* msg = createLoRaChatMessage();
    msg->addrDst = dst;
    msg->type = LoRaChatMessageType::requestContactInfo;
    msg->appPortDst = appPort::LoRaChat;

    MessageManager::getInstance().sendMessage(port, (DataMessage*) msg);

    delete msg;
}

LoRaChatMessage* LoRaChatService::createLoRaChatMessage() {
    LoRaChatMessage* msg = new LoRaChatMessage();

    msg->appPortSrc = appPort::LoRaChat;
    msg->messageId = requestId;
    msg->addrSrc = LoraMesher::getInstance().getLocalAddress();
    msg->messageSize = sizeof(LoRaChatMessage);

    requestId++;

    return msg;
}

LoRaChatMessage* LoRaChatService::createLoRaChatMessage(String message) {
    if (message.length() > MAX_MESSAGE_LENGTH)
        return nullptr;

    uint32_t size = sizeof(LoRaChatMessage) + message.length() + 1; //TODO: +1 is for the null terminator, is this needed?

    LoRaChatMessage* msg = (LoRaChatMessage*) malloc(size);

    msg->appPortSrc = appPort::LoRaChat;
    msg->messageId = requestId;
    msg->addrSrc = LoraMesher::getInstance().getLocalAddress();
    msg->messageSize = size;

    requestId++;

    return msg;
}

void LoRaChatService::responseContactInfo(messagePort port, LoRaChatMessage* message) {
    LoRaChatMessageInfo* response = (LoRaChatMessageInfo*) malloc(sizeof(LoRaChatMessageInfo) + MAX_NAME_LENGTH);
    memcpy(response, message, sizeof(LoRaChatMessageInfo));

    response->appPortDst = message->appPortSrc;
    response->appPortSrc = appPort::LoRaChat;
    response->addrSrc = LoraMesher::getInstance().getLocalAddress();
    response->addrDst = message->addrSrc;

    response->messageSize = sizeof(LoRaChatMessageInfo);

    response->type = LoRaChatMessageType::responseContactInfo;

    memcpy(response->name, myName, MAX_NAME_LENGTH);

    MessageManager::getInstance().sendMessage(port, (DataMessage*) response);

    free(response);
}
