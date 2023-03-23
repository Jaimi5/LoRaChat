#pragma once

#include <Arduino.h>

#include "config.h"

class Helper {
public:

    /**
     * @brief Uint8 to String
     *
     * @param array The array to convert
     * @param length The length of the array
     * @return String The converted string
     */
    static String uint8ArrayToString(uint8_t* array, uint8_t length);

    /**
     * @brief Blink the LED
     *
     * @param times Number of times to blink
     * @param delayTime Delay between blinks
     * @param pin The pin to blink
     */
    static void ledBlink(uint8_t times, uint16_t delayTime, uint8_t pin = LED_BUILTIN);
};