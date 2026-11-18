#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#pragma pack(1)

enum BluetoothMessageType : uint8_t {
    // contactRequest = 1,
    // contactResponse = 2,
    // sendMessage = 3,
    // gpsMessage = 4,
    // SOSMessage = 5,
    bluetoothMessage = 1
};

class BluetoothMessage : public DataMessageGeneric {
public:
    BluetoothMessageType type;
    uint8_t message[];

    // Length of message[]. messageSize counts the payload only (type + message), not the
    // DataMessage header. Capped to what the uint8_t length of the helpers can express.
    uint8_t getPayloadSize() {
        if (messageSize <= sizeof(BluetoothMessageType))
            return 0;

        uint32_t size = messageSize - sizeof(BluetoothMessageType);
        return size > UINT8_MAX ? UINT8_MAX : (uint8_t)size;
    }
};

#pragma pack()