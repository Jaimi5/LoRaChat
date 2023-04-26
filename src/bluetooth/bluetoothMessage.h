#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#pragma pack(1)

enum BluetoothMessageType: uint8_t {
    // contactRequest = 1,
    // contactResponse = 2,
    // sendMessage = 3,
    // gpsMessage = 4,
    // SOSMessage = 5,
    bluetoothMessage = 1
};

class BluetoothMessage: public DataMessageGeneric {
public:
    BluetoothMessageType type;
    uint8_t message[];

    uint8_t getPayloadSize() {
        return messageSize - sizeof(BluetoothMessage);
    }
};

#pragma pack()