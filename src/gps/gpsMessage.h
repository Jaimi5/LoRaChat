#pragma once

#include <Arduino.h>

#include "./message/dataMessage.h"

enum GPSMessageType: uint8_t {
    getGPS = 1,
    responseGPS = 2
};

#pragma pack(1)

class GPSMessageRequest: public DataMessageGeneric {
public:
    GPSMessageType type;
};

class GPSMessageResponse: public GPSMessageRequest {
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