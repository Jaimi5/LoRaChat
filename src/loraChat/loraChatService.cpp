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
    String defaultName = String(LoRaMeshService::getInstance().getDeviceID());
    //Add the defaultName to the variable myName String to Char array
    defaultName.toCharArray(myName, MAX_NAME_LENGTH);
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

    BluetoothService::getInstance().writeToBluetooth(String("New contact: ") + String(ci->name) + String(" - ") + String(src));
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
    //TODO: I don't know if this is the best way to do it
    message->appPortDst = appPort::GPSApp;
    GPSMessageGeneric* gpsMessage = (GPSMessageGeneric*) message;
    gpsMessage->type = GPSMessageType::reqGPS;
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

    uint32_t msgSize = msg->messageSize - (sizeof(LoRaChatMessageGeneric) + sizeof(DataMessageGeneric));

    //Check for message size
    if (msgSize > MAX_MESSAGE_LENGTH) {
        return F("Message too long");
    }

    String messageStringed = String(msg->message, msgSize);

    String chatMessage = name + ": " + messageStringed + "\n";

    addPreviousMessage(msg->addrSrc, messageStringed);

    BluetoothService::getInstance().writeToBluetooth(chatMessage);

    Serial.println(chatMessage);

    return "Message received";
}

void LoRaChatService::addPreviousMessage(uint16_t address, String message) {
    uint32_t actualTime = millis();

    //Iterate through previousMessage attribute, find the first empty message or the oldest message
    uint8_t oldestMessageIndex = 0;
    uint32_t oldestMessageTime = UINT32_MAX;

    for (uint8_t i = 0; i < MAX_PREVIOUS_MESSAGES; i++) {
        if (previousMessage[i] == nullptr) {
            oldestMessageIndex = i;
            break;
        }
        else {
            uint32_t messageTime = previousMessage[i]->time;
            if (messageTime < oldestMessageTime) {
                oldestMessageTime = messageTime;
                oldestMessageIndex = i;
            }
        }
    }

    //If previousMessage exists, delete it
    if (previousMessage[oldestMessageIndex] != nullptr) {
        delete previousMessage[oldestMessageIndex];
    }

    //Create new previousMessage
    previousMessage[oldestMessageIndex] = new PreviousMessage(address, actualTime, String(message));
}

void LoRaChatService::orderPreviousMessagesByTime() {
    //Order previousMessages by time
    for (uint8_t i = 0; i < MAX_PREVIOUS_MESSAGES; i++) {
        if (previousMessage[i] == nullptr)
            break;

        for (uint8_t j = i + 1; j < MAX_PREVIOUS_MESSAGES; j++) {
            if (previousMessage[j] == nullptr)
                break;

            if (previousMessage[i]->time > previousMessage[j]->time) {
                PreviousMessage* aux = previousMessage[i];
                previousMessage[i] = previousMessage[j];
                previousMessage[j] = aux;
            }
        }
    }
}

String LoRaChatService::previousMessageToString(PreviousMessage* previousMessage) {
    String message = previousMessage->message;
    String name = getNameContact(previousMessage->address);
    uint32_t timeSinceMessage = millis() - previousMessage->time;
    String time = TimeHelper::getReadableTime(timeSinceMessage);

    if (name.length() == 0)
        name = String(previousMessage->address);

    return "(Received " + time + " ago) " + name + ": " + message;
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

String LoRaChatService::getPreviousMessages() {
    String previousMessages = "Previous messages:\n";

    //Iterate through previousMessages ordered by time
    orderPreviousMessagesByTime();

    for (uint8_t i = 0; i < MAX_PREVIOUS_MESSAGES; i++) {
        if (previousMessage[i] == nullptr)
            break;

        previousMessages += previousMessageToString(previousMessage[i]) + "\n";
    }

    return previousMessages;
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
    msg->messageSize = sizeof(LoRaChatMessage) - sizeof(DataMessageGeneric);

    requestId++;

    return msg;
}

LoRaChatMessage* LoRaChatService::createLoRaChatMessage(String message) {
    if (message.length() > MAX_MESSAGE_LENGTH)
        return nullptr;

    uint32_t size = sizeof(LoRaChatMessage) + message.length() + 1;

    LoRaChatMessage* msg = (LoRaChatMessage*) malloc(size);
    memcpy(msg->message, message.c_str(), message.length() + 1);

    msg->appPortSrc = appPort::LoRaChat;
    msg->messageId = requestId;
    msg->addrSrc = LoraMesher::getInstance().getLocalAddress();
    msg->messageSize = size - sizeof(DataMessageGeneric);

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

    response->messageSize = sizeof(LoRaChatMessageInfo) - sizeof(DataMessageGeneric);

    response->type = LoRaChatMessageType::responseContactInfo;

    memcpy(response->name, myName, MAX_NAME_LENGTH);

    MessageManager::getInstance().sendMessage(port, (DataMessage*) response);

    free(response);
}
