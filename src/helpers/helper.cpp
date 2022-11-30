#include "helper.h"

String Helper::uint8ArrayToString(uint8_t* array, uint8_t length) {
    String result = "";
    for (uint8_t i = 0; i < length; i++) {
        result += (char) array[i];
    }
    return result;
}

void Helper::ledBlink(uint8_t times, uint16_t delayTime, uint8_t pin) {
    pinMode(BOARD_LED, OUTPUT);

    for (uint16_t index = 0; index < times; index++) {
        digitalWrite(BOARD_LED, LED_ON);
        vTaskDelay(delayTime / portTICK_PERIOD_MS);
        digitalWrite(BOARD_LED, LED_OFF);
        vTaskDelay(delayTime / portTICK_PERIOD_MS);
    }
}
