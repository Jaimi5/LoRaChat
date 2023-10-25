#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "bluetoothMessage.h"

//Bluetooth
#include <BluetoothSerial.h>

class BluetoothCommandService: public CommandService {
public:

    BluetoothCommandService();
};