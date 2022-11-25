#pragma once

#include <Arduino.h>

//Message Ports
enum messagePort: uint8_t {
    LoRaMeshPort = 1,
    BluetoothPort = 2,
    WiFiPort = 3
};

//TODO: This should be defined by the user, all the apps that are available and their numbers should be the same
//TODO: in all the nodes of the network.
enum appPort: uint8_t {
    LoRaChat = 1,
    BluetoothApp = 2,
    WiFiApp = 3,
    GPSApp = 4,
    SOSApp = 5,
    CommandApp = 6,
    LoRaMesherApp = 7
};

#pragma pack(1)


class DataMessageGeneric {
public:
    appPort appPortDst;
    appPort appPortSrc;
    uint8_t messageId;

    uint16_t addrSrc;
    uint16_t addrDst;

    uint32_t messageSize;

    uint8_t type;

    uint32_t getDataMessageSize() {
        return sizeof(DataMessageGeneric) + messageSize;
    }
};

class DataMessage: public DataMessageGeneric {
public:
    uint8_t message[];
};

#pragma pack()