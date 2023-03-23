#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#pragma pack(1)

enum MqttMessageType: uint8_t {
    mqttMessage = 1
};

class MqttMessage: public DataMessageGeneric {
public:
    MqttMessageType type;
    uint8_t message[];

    uint8_t getPayloadSize() {
        return messageSize - sizeof(MqttMessage);
    }
};

#pragma pack()