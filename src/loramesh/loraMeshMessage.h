#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#pragma pack(1)

class LoRaMeshMessage {
public:
    appPort appPortDst;
    appPort appPortSrc;
    uint8_t messageId;
    uint8_t dataMessage[];
};
#pragma pack()
