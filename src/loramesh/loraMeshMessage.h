#pragma once

#include <Arduino.h>

#include "./message/dataMessage.h"

#include "LoraMesher.h"

enum LoRaMeshMessageType: uint8_t {
    sendMessage = 1,
    getRoutingTable = 2,
};
//TODO: This could be optimized, now we are sending two times id, srcAddr, dstAddr and messageSize

#pragma pack(1)
class LoRaMeshMessage {
public:
    appPort appPortDst;
    appPort appPortSrc;
    uint8_t messageId;
    uint8_t dataMessage[];
};
#pragma pack()



