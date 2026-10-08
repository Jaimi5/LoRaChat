#include "config.h"
#ifdef BLUETOOTH_ENABLED
#include "bluetoothCommandService.h"
#include "bluetoothService.h"

BluetoothCommandService::BluetoothCommandService() {
    // Send command to bluetooth
    addCommand(Command("/sendB", "Send a message to the bluetooth device",
                       BluetoothMessageType::bluetoothMessage, 1, [this](String args) {
                           return BluetoothService::getInstance().writeToBluetooth(args)
                                      ? "Message sent"
                                      : "Device not connected";
                       }));
}

#endif  // BLUETOOTH_ENABLED
