#pragma once

#include <Arduino.h>

#include "./message/dataMessage.h"

enum BluetoothMessageType: uint8_t {
    // contactRequest = 1,
    // contactResponse = 2,
    // sendMessage = 3,
    // gpsMessage = 4,
    // SOSMessage = 5,
    bluetoothMessage = 1
};

#pragma pack(1)

class BluetoothMessage: public DataMessage {
public:
    BluetoothMessageType type;
    uint8_t payload[];

    uint8_t getPayloadSize() {
        return payloadSize - sizeof(BluetoothMessageType);
    }
};

#pragma pack()