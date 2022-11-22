#pragma once

#include <Arduino.h>

#include "./message/dataMessage.h"

enum LoRaMeshMessageType: uint8_t {
    sendMessage = 1,
    getRoutingTable = 2,
};

#pragma pack(1)
class LoRaMesherMessage: public DataMessage {
public:
    uint8_t payload[];
};
#pragma pack()