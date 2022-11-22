#include "bluetoothCommandService.h"
#include "bluetoothService.h"

BluetoothCommandService::BluetoothCommandService() {
    //Send command to bluetooth
    addCommand(Command("/sendB", "Send a message to the bluetooth device", BluetoothMessageType::bluetoothMessage, 1,
        [this](String args) {
        return BluetoothService::getInstance().writeToBluetooth(args) ? "Message sent" : "Device not connected";
    }));

    // this->addCommand(Command("/getLocation", "Get current location", BluetoothMessageType::gpsMessage, 1,
    //     [this](String args) { return this->getLocationCommand(); }));


    // this->addCommand(Command("/getLocation", "Get the current location", &getLocationCommand));
    // this->addCommand(Command("/searchContacts", "Search for a contact", &searchContactsCommand));
    // this->addCommand(Command("/printContacts", "Print all contacts", &printContactsCommand));
    // this->addCommand(Command("/changeName", "Change the name", &changeNameCommand));
    // this->addCommand(Command("/chat", "Chat with a contact", &chatCommand));
    // this->addCommand(Command("/printRT", "Print the routing table", &printRTCommand));
}

String BluetoothCommandService::getLocationCommand() {
    return GPSService::getInstance().getGPSUpdatedWait();
}

String BluetoothCommandService::searchContactsCommand() {
    Serial.println("searchContacts");
    return "searchContacts";
}

String BluetoothCommandService::printContactsCommand() {
    Serial.println("printContacts");
    return "printContacts";
}

String BluetoothCommandService::changeNameCommand() {
    Serial.println("changeName");
    return "changeName";
}

String BluetoothCommandService::chatCommand() {

    Serial.println("chat");
    return "chat";
}

String BluetoothCommandService::printRTCommand() {
    Serial.println("printRT");
    return "printRT";
}