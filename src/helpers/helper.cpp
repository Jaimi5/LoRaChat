#include "helper.h"

String Helper::uint8ArrayToString(uint8_t* array, uint8_t length) {
    String result = "";
    for (uint8_t i = 0; i < length; i++) {
        result += (char) array[i];
    }
    return result;
}

void Helper::ledBlink(uint8_t times, uint16_t delayTime, uint8_t pin) {
    pinMode(LED_BUILTIN, OUTPUT);

    for (uint16_t index = 0; index < times; index++) {
        digitalWrite(LED_BUILTIN, HIGH);
        vTaskDelay(delayTime / portTICK_PERIOD_MS);
        digitalWrite(LED_BUILTIN, LOW);
        vTaskDelay(delayTime / portTICK_PERIOD_MS);
    }
}