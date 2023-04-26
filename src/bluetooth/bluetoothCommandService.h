#pragma once

#include "Arduino.h"

#include "commands/commandService.h"

#include "bluetoothMessage.h"

class BluetoothCommandService: public CommandService {
public:

    BluetoothCommandService();
};