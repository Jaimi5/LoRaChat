#pragma once

#include <Arduino.h>

#include "./config.h"

#include "./message/dataMessage.h"

enum ContactMessageType: uint8_t {
    changeName = 1,
    getName = 2,
    getContacts = 3,
    getAddrByName = 4,
    requestContactInfo = 5,
    responseContactInfo = 6,
    myContact = 7,
    chatTo = 8,
    requestGPS = 9,
    responseGPS = 10
};

#pragma pack(1)

class ContactMessageGeneric: public DataMessageGeneric {
public:
    ContactMessageType type;
};

class ContactMessage: public ContactMessageGeneric {
public:
    uint8_t payload[];
};

class ContactMessageInfo: public ContactMessageGeneric {
public:
    uint8_t name[MAX_NAME_LENGTH];
};

#pragma pack()