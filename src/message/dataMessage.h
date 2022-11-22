#pragma once

#include <Arduino.h>

//Message Ports
enum messagePort: uint8_t {
    LoRaMeshPort = 1,
    BluetoothPort = 2,
    WiFiPort = 3
};

//TODO: This should be defined by the user, all the apps that are available and their numbers should be the same in all the nodes of the network.
enum appPort: uint8_t {
    ContactApp = 1,
    BluetoothApp = 2,
    WiFiApp = 3,
    GPSApp = 4,
    SOSApp = 5,
    CommandApp = 6
};

#pragma pack(1)

class DataMessage {
public:
    uint16_t dst;
    uint8_t src;
    appPort port;
    uint8_t payloadSize;
};

class ManagerMessage {
public:
    messagePort port;
    DataMessage* message;
};

#pragma pack()