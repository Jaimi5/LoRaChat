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

        JsonObject gps = doc.createNestedObject("gps");
        gps["latitude"] = latitude;
        gps["longitude"] = longitude;
        gps["altitude"] = altitude;
        gps["satellite_number"] = satellites;

        if (day == 0) {
            day = 1;
        }

        if (month == 0) {
            month = 1;
        }

        char isoTime[27]; // Increased size to accommodate the maximum possible length of the formatted string
        snprintf(isoTime, sizeof(isoTime), "%04d-%02d-%02dT%02d:%02d:%02dZ",
            year, month, day, hour, minute, second);

        doc["timestamp"] = isoTime;
    }

    // String setLeadingZeroes(uint8_t num, bool isHour = false) {
    //     if (!isHour && num == 0)
    //         return "01";

    //     char numBuffer[3];
    //     sprintf(numBuffer, "%02d", num);

    //     return String(numBuffer);
    // }
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