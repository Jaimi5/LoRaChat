#include "contactCommandService.h"
#include "contactService.h"

ContactCommandService::ContactCommandService() {
    //Add command to change name
    addCommand(Command("/changeName", "Change the name of the device", ContactMessageType::changeName, 1,
        [this](String args) {
        ContactService::getInstance().changeName(args);
    return String("Name changed to ") + args;
    }));

    //Add command to get contacts
    addCommand(Command("/getContacts", "Get the contacts of the device", ContactMessageType::getContacts, 1,
        [this](String args) {
        return ContactService::getInstance().getContactsString();
    }));

    addCommand(Command("/reqContacts", "Find the contacts that use bluetooth", ContactMessageType::requestContactInfo, 1,
        [this](String args) {
        return ContactService::getInstance().findContacts();
    }));

    addCommand(Command("/requestGPSOf", "Request the GPS by name of the device", ContactMessageType::requestGPS, 1,
        [this](String args) {
        return ContactService::getInstance().requestGPS(messagePort::LoRaMeshPort, args);
    }));
}