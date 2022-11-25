#include "contactService.h"

void ContactService::processReceivedMessage(messagePort port, DataMessage* message) {
    ContactMessage* request = (ContactMessage*) message;
    switch (request->type) {
        case ContactMessageType::requestContactInfo:
            responseContactInfo(port, request);
            break;

        case ContactMessageType::responseContactInfo:
            addContact(message);
            break;

        case ContactMessageType::requestGPS:
            responseGPS(port, message);
            break;

        default:
            break;
    }
}

void ContactService::initContactService() {

}

void ContactService::addContact(DataMessage* message) {
    ContactMessageInfo* contactMessage = (ContactMessageInfo*) message;
    addContact(String(contactMessage->name, MAX_NAME_LENGTH), message->addrSrc);
}

void ContactService::addContact(String name, uint16_t src) {
    contactsList->setInUse();
    if (contactsList->moveToStart()) {
        do {
            Info* ci = contactsList->getCurrent();
            if (ci) {
                if (ci->address == src) {
                    name.toCharArray(ci->name, MAX_NAME_LENGTH);
                    contactsList->releaseInUse();
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
}

String ContactService::getNameContact(uint16_t addr) {
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

uint16_t ContactService::getAddrContact(String name) {
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

String ContactService::getContactsString() {
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

void ContactService::changeName(String newName) {
    newName.toCharArray(myName, MAX_NAME_LENGTH);
}

String ContactService::requestGPS(messagePort port, String name) {
    uint16_t addr = getAddrContact(name);
    if (addr == 0) {
        return String("No contact found");
    }

    ContactMessage* msg = createContactMessage();
    msg->addrDst = addr;
    msg->type = ContactMessageType::requestGPS;
    msg->appPortDst = appPort::LoRaChat;

    MessageManager::getInstance().sendMessage(port, (DataMessage*) msg);

    delete msg;

    return "Request GPS sent, waiting for response";
}

String ContactService::responseGPS(messagePort port, DataMessage* message) {
    message->appPortDst = appPort::GPSApp;
    message->type = GPSMessageType::reqGPS;
    GPSService::getInstance().processReceivedMessage(port, message);

    return "GPS requested";
}

String ContactService::findContacts() {
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

void ContactService::requestContactInfo(messagePort port, uint16_t dst) {
    ContactMessage* msg = createContactMessage();
    msg->addrDst = dst;
    msg->type = ContactMessageType::requestContactInfo;
    msg->appPortDst = appPort::LoRaChat;

    MessageManager::getInstance().sendMessage(port, (DataMessage*) msg);

    delete msg;
}

ContactMessage* ContactService::createContactMessage() {
    ContactMessage* msg = new ContactMessage();

    msg->appPortSrc = appPort::LoRaChat;
    msg->messageId = requestId;
    msg->addrSrc = LoraMesher::getInstance().getLocalAddress();
    msg->messageSize = sizeof(ContactMessage);

    requestId++;

    return msg;
}

void ContactService::responseContactInfo(messagePort port, ContactMessage* message) {
    ContactMessageInfo* response = (ContactMessageInfo*) malloc(sizeof(ContactMessageInfo) + MAX_NAME_LENGTH);
    memcpy(response, message, sizeof(ContactMessageInfo));

    response->appPortDst = message->appPortSrc;
    response->appPortSrc = appPort::LoRaChat;
    response->addrSrc = LoraMesher::getInstance().getLocalAddress();
    response->addrDst = message->addrSrc;

    response->messageSize = sizeof(ContactMessageInfo);

    response->type = ContactMessageType::responseContactInfo;

    memcpy(response->name, myName, MAX_NAME_LENGTH);

    MessageManager::getInstance().sendMessage(port, (DataMessage*) response);

    free(response);
}
