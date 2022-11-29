#include "bluetoothCommandService.h"
#include "bluetoothService.h"

BluetoothCommandService::BluetoothCommandService() {
    //Send command to bluetooth
    addCommand(Command(F("/sendB"), F("Send a message to the bluetooth device"), BluetoothMessageType::bluetoothMessage, 1,
        [this](String args) {
        return BluetoothService::getInstance().writeToBluetooth(args) ? F("Message sent") : F("Device not connected");
    }));
}