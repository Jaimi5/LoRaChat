#pragma once

#include <Arduino.h>

class TimeHelper {
public:
    static String getReadableTime(uint8_t seconds, uint8_t minutes, uint8_t hours);

    static String getReadableDate(uint8_t day, uint8_t month, uint16_t year);

    static String getReadableTime(uint32_t millis);
};