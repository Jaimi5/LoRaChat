#pragma once

#include <Arduino.h>

#include "./message/dataMessage.h"

#pragma pack(1)

enum GPSMessageType: uint8_t {
    reqGPS = 1,
    getGPS = 2
};

class GPSMessageGeneric: public DataMessageGeneric {
public:
    GPSMessageType type;
};

class GPSMessageResponse: public GPSMessageGeneric {
public:
    double latitude;
    double longitude;
    double altitude;
    uint8_t satellites;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};

#pragma pack()