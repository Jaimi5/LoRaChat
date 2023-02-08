#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#include "LoraMesher.h"

#pragma pack(1)

enum LoRaMeshMessageType: uint8_t {
    sendMessage = 1,
    getRoutingTable = 2,
};

class LoRaMeshMessage {
public:
    appPort appPortDst;
    appPort appPortSrc;
    uint8_t messageId;
    uint8_t dataMessage[];
};
#pragma pack()



