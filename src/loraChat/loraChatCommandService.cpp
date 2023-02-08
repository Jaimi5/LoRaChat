#include "loraChatCommandService.h"
#include "loraChatService.h"

LoRaChatCommandService::LoRaChatCommandService() {
    //Add command to change name
    addCommand(Command("/changeName", "Change the name of the device", LoRaChatMessageType::changeName, 1,
        [this](String args) {
        return LoRaChatService::getInstance().changeName(args);
    }));

    //Add command to get contacts
    addCommand(Command("/getContacts", "Get the contacts of the device", LoRaChatMessageType::getContacts, 1,
        [this](String args) {
        return LoRaChatService::getInstance().getContactsString();
    }));

    addCommand(Command("/reqContacts", "Find the contacts that use bluetooth", LoRaChatMessageType::requestContactInfo, 1,
        [this](String args) {
        return LoRaChatService::getInstance().findContacts();
    }));

    addCommand(Command("/chat", "Start a chat with a contact", LoRaChatMessageType::chatTo, 1,
        [this](String args) {
        return LoRaChatService::getInstance().chatTo(args);
    }));

    addCommand(Command("/requestGPSOf", "Request the GPS by name of the device", LoRaChatMessageType::requestGPS, 1,
        [this](String args) {
        return LoRaChatService::getInstance().requestGPS(messagePort::LoRaMeshPort, args);
    }));

    addCommand(Command("/getPreviousMessages", "Get the previous messages", LoRaChatMessageType::getPreviousMessages, 1,
        [this](String args) {
        return LoRaChatService::getInstance().getPreviousMessages();
    }));

    addCommand(Command("/sendChatToWifi", "Send the chat to the wifi", LoRaChatMessageType::sendChatToWifi, 1,
        [this](String args) {
        return LoRaChatService::getInstance().sendChatToWifi(args);
    }));
}