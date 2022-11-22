#include "helper.h"

String Helper::uint8ArrayToString(uint8_t* array, uint8_t length) {
    String result = "";
    for (uint8_t i = 0; i < length; i++) {
        result += (char) array[i];
    }
    return result;
}