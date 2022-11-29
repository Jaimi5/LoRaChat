#include "loraChatCommandService.h"
#include "loraChatService.h"

LoRaChatCommandService::LoRaChatCommandService() {
    //Add command to change name
    addCommand(Command(F("/changeName"), F("Change the name of the device"), LoRaChatMessageType::changeName, 1,
        [this](String args) {
        return LoRaChatService::getInstance().changeName(args);
    }));

    //Add command to get contacts
    addCommand(Command(F("/getContacts"), F("Get the contacts of the device"), LoRaChatMessageType::getContacts, 1,
        [this](String args) {
        return LoRaChatService::getInstance().getContactsString();
    }));

    addCommand(Command(F("/reqContacts"), F("Find the contacts that use bluetooth"), LoRaChatMessageType::requestContactInfo, 1,
        [this](String args) {
        return LoRaChatService::getInstance().findContacts();
    }));

    addCommand(Command(F("/chat"), F("Start a chat with a contact"), LoRaChatMessageType::chatTo, 1,
        [this](String args) {
        return LoRaChatService::getInstance().chatTo(args);
    }));

    addCommand(Command(F("/requestGPSOf"), F("Request the GPS by name of the device"), LoRaChatMessageType::requestGPS, 1,
        [this](String args) {
        return LoRaChatService::getInstance().requestGPS(messagePort::LoRaMeshPort, args);
    }));
}