#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#ifndef USE_LORAMESHER_V2
#include "LoraMesher.h"
#endif

#pragma pack(1)

enum LoRaMeshMessageType : uint8_t {
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
