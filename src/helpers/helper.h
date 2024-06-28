#pragma once

#include <Arduino.h>

#include "config.h"

class Helper {
public:
    static String uint8ArrayToString(uint8_t* array, uint8_t length);
    static void stringToByteArray(String data, uint8_t* result);
    static String uint8ArrayToHexString(uint8_t* array, uint8_t length);

    static void ledBlink(uint8_t times, uint16_t delayTime, uint8_t pin = LED);

    static String longDecimalToHexString(unsigned long long);
    static String pad32Bytes(String);

    static void utf8ToByteArray(String data, uint8_t* array);
    static void hex2bin(const char* src, char* target);
    static int char2int(char src);
    static void printHex(uint8_t* data, int length, String title);
};