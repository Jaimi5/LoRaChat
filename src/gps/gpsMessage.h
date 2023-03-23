#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#pragma pack(1)

enum GPSMessageType: uint8_t {
    reqGPS = 1,
    getGPS = 2
};

class GPSMessage {
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

    void serialize(JsonObject& doc) {
        doc["latitude"] = latitude;
        doc["longitude"] = longitude;
        doc["altitude"] = altitude;
        doc["satellites"] = satellites;
        doc["hour"] = hour;
        doc["minute"] = minute;
        doc["second"] = second;
        doc["day"] = day;
        doc["month"] = month;
        doc["year"] = year;
    }
};

class GPSMessageGeneric: public DataMessageGeneric {
public:
    GPSMessageType type;
};

class GPSMessageResponse: public GPSMessageGeneric {
public:
    GPSMessage gps;
};

#pragma pack()