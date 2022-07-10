#pragma once

#include <Arduino.h>

String getReadableTime(uint8_t seconds, uint8_t minutes, uint8_t hours) {
    String readableTime;

    if (hours < 10)
        readableTime += "0";

    readableTime += String(hours) + ":";

    if (minutes < 10)
        readableTime += "0";

    readableTime += String(minutes) + ":";

    if (seconds < 10)
        readableTime += "0";

    readableTime += String(seconds);

    return readableTime;
}