#include "timeHelper.h"

String TimeHelper::getReadableTime(uint8_t seconds, uint8_t minutes, uint8_t hours) {
    String readableTime;

    readableTime += (hours < 10) ? "0" : "";
    readableTime += String(hours) + ":";
    readableTime += (minutes < 10) ? "0" : "";
    readableTime += String(minutes) + ":";
    readableTime += (seconds < 10) ? "0" : "";
    readableTime += String(seconds);

    return readableTime;
}

String TimeHelper::getReadableDate(uint8_t day, uint8_t month, uint16_t year) {
    String readableDate;

    readableDate += (day < 10) ? "0" : "";
    readableDate += String(day) + "/";
    readableDate += (month < 10) ? "0" : "";
    readableDate += String(month) + "/";
    readableDate += String(year);

    return readableDate;
}

String TimeHelper::getReadableTime(uint32_t millis) {
    uint32_t seconds = millis / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;

    return getReadableTime(seconds % 60, minutes % 60, hours % 24);
}