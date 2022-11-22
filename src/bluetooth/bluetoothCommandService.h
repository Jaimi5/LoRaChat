#pragma once

#include "Arduino.h"

#include "./commands/commandService.h"

#include "bluetoothMessage.h"

//Bluetooth
#include <BluetoothSerial.h>

//TODO: GPS Service here?
#include "./gps/gpsService.h"


class BluetoothCommandService: public CommandService {
public:

    BluetoothCommandService();

private:
    String getLocationCommand();
    static String searchContactsCommand();
    static String printContactsCommand();
    static String changeNameCommand();
    static String chatCommand();
    static String printRTCommand();
};