#pragma once

#include <Arduino.h>

#include "./config.h"

#pragma pack(1)
class Info {
public:
    uint16_t address;
    char name[MAX_NAME_LENGTH];
};

class PreviousMessage {
public:
    PreviousMessage(uint16_t address, uint32_t time, String message): address(address), time(time), message(message) {};

    uint16_t address;
    uint32_t time;
    String message;
};
#pragma pack()