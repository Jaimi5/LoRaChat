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
        digitalWrite(BOARD_LED, LED_OFF);
        vTaskDelay(delayTime / portTICK_PERIOD_MS);
        digitalWrite(BOARD_LED, LED_ON);
        vTaskDelay(delayTime / portTICK_PERIOD_MS);
    }
}

void Helper::stringToByteArray(String data, uint8_t* result) {
    uint8_t buff;
    char aux;
    int j = 0;
    for (int i = 0; i < data.length(); i = i + 2) {
        buff = 0;
        aux = data[i];
        if (aux >= '0' and aux <= '9') buff = (buff + (aux - '0')) * 16;
        else if (aux >= 'A' and aux <= 'Z') buff = (buff + (aux - 'A' + 10)) * 16;
        else if (aux >= 'a' and aux <= 'z') buff = (buff + (aux - 'a' + 10)) * 16;
        aux = data[i + 1];
        if (aux >= '0' and aux <= '9') buff = (buff + (aux - '0'));
        else if (aux >= 'A' and aux <= 'Z') buff = (buff + (aux - 'A' + 10));
        else if (aux >= 'a' and aux <= 'z') buff = (buff + (aux - 'a' + 10));
        result[j] = buff;
        j++;
    }
}

String Helper::longDecimalToHexString(unsigned long long n) {
    String hex_value = "";
    char buf[50];
    sprintf(buf, "%llu", n);
    while (n > 0) {
        int temp = 0;
        int  divisor = 16;
        char aux;
        temp = n % divisor;
        if (temp < 10) {
            aux = temp + '0';
            hex_value = aux + hex_value;
        }
        else {
            aux = (temp - 10) + 'A';
            hex_value = aux + hex_value;
        }

        n = n / divisor;
    }
    if (hex_value.length() % 2 > 0) hex_value = "0" + hex_value;
    return hex_value;
}

String Helper::pad32Bytes(String data) {
    String s = String(data);
    while (s.length() < 64) { s = "0" + s; }
    return s;
}

String Helper::intToHexString(int n) {
    String hex_value = "";
    char buf[50];
    sprintf(buf, "%llu", n);
    while (n > 0) {
        int temp = 0;
        int  divisor = 16;
        char aux;
        temp = n % divisor;
        if (temp < 10) {
            aux = temp + '0';
            hex_value = aux + hex_value;
        }
        else {
            aux = (temp - 10) + 'A';
            hex_value = aux + hex_value;
        }

        n = n / divisor;
    }
    if (hex_value.length() % 2 > 0) hex_value = "0" + hex_value;
    return hex_value;
}
