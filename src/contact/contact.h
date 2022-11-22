#pragma once

#include <Arduino.h>

#define MAX_NAME_LENGTH 10

#pragma pack(1)
class Info {
public:
    uint16_t address;
    char name[MAX_NAME_LENGTH];
};
#pragma pack()