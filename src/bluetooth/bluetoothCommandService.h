#pragma once

#include "Arduino.h"

#include "./commands/commandService.h"

//Bluetooth
#include <BluetoothSerial.h>

//TODO: GPS Service here?
#include "./gps/gpsService.h"

class BluetoothCommandService: public CommandService {
public:

    BluetoothCommandService();

    //TODO: Implement
    String execute(String command);

    //TODO: Implement
    bool back();

private:
    String getLocationCommand();
    static String searchContactsCommand();
    static String printContactsCommand();
    static String changeNameCommand();
    static String chatCommand();
    static String printRTCommand();
};