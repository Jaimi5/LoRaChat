#include "loraChatCommandService.h"
#include "loraChatService.h"

LoRaChatCommandService::LoRaChatCommandService() {
    //Add command to change name
    addCommand(Command("/changeName", "Change the name of the device", ContactMessageType::changeName, 1,
        [this](String args) {
        return LoRaChatService::getInstance().changeName(args);
    }));

    //Add command to get contacts
    addCommand(Command("/getContacts", "Get the contacts of the device", ContactMessageType::getContacts, 1,
        [this](String args) {
        return LoRaChatService::getInstance().getContactsString();
    }));

    addCommand(Command("/reqContacts", "Find the contacts that use bluetooth", ContactMessageType::requestContactInfo, 1,
        [this](String args) {
        return LoRaChatService::getInstance().findContacts();
    }));

    addCommand(Command("/requestGPSOf", "Request the GPS by name of the device", ContactMessageType::requestGPS, 1,
        [this](String args) {
        return LoRaChatService::getInstance().requestGPS(messagePort::LoRaMeshPort, args);
    }));
}