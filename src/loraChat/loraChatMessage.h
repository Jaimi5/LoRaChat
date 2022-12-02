#pragma once

#include <Arduino.h>

#include "./config.h"

#include "./message/dataMessage.h"

#pragma pack(1)

enum LoRaChatMessageType: uint8_t {
    changeName = 1,
    getName = 2,
    getContacts = 3,
    getAddrByName = 4,
    requestContactInfo = 5,
    responseContactInfo = 6,
    myContact = 7,
    chatTo = 8,
    ackChat = 9,
    requestGPS = 10,
    responseGPS = 11,
    getPreviousMessages = 12
};

class LoRaChatMessageGeneric: public DataMessageGeneric {
public:
    LoRaChatMessageType type;
};

class LoRaChatMessage: public LoRaChatMessageGeneric {
public:
    uint8_t message[];
};

class LoRaChatMessageInfo: public LoRaChatMessageGeneric {
public:
    uint8_t name[MAX_NAME_LENGTH];
};

#pragma pack()