#pragma once

#include <Arduino.h>

#include "config.h"

class Helper {
public:
    static String uint8ArrayToString(uint8_t* array, uint8_t length);
    static void stringToByteArray(String data, uint8_t* result);

    static void ledBlink(uint8_t times, uint16_t delayTime, uint8_t pin = BOARD_LED);

    static String longDecimalToHexString(unsigned long long);
    static String intToHexString(int n);
    static String pad32Bytes(String);

};