#pragma once

#include <Arduino.h>

#include "./config.h"

#pragma pack(1)
class Info {
public:
    uint16_t address;
    char name[MAX_NAME_LENGTH];
};
#pragma pack()