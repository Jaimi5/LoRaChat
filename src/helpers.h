#pragma once

#include <Arduino.h>

String getReadableTime(uint8_t seconds, uint8_t minutes, uint8_t hours) {
    String readableTime;

    readableTime += (hours < 10) ? "0" : "";
    readableTime += String(hours) + ":";
    readableTime += (minutes < 10) ? "0" : "";
    readableTime += String(minutes) + ":";
    readableTime += (seconds < 10) ? "0" : "";
    readableTime += String(seconds);

    return readableTime;
}