#include "helper.h"

String Helper::uint8ArrayToString(uint8_t* array, uint8_t length) {
    String result = "";
    for (uint8_t i = 0; i < length; i++) {
        result += (char) array[i];
    }
    return result;
}

void Helper::ledBlink(uint8_t times, uint16_t delayTime, uint8_t pin) {
    pinMode(LED, OUTPUT);

    for (uint16_t index = 0; index < times; index++) {
        digitalWrite(LED, LED_ON);
        vTaskDelay(delayTime / portTICK_PERIOD_MS);
        digitalWrite(LED, LED_OFF);
        vTaskDelay(delayTime / portTICK_PERIOD_MS);
    }
}

// void Helper::stringToByteArray(String data, uint8_t* result) {
//     uint8_t buff;
//     char aux;
//     int j = 0;
//     for (int i = 0; i < data.length(); i = i + 2) {
//         buff = 0;
//         aux = data[i];
//         if (aux >= '0' and aux <= '9') buff = (buff + (aux - '0')) * 16;
//         else if (aux >= 'A' and aux <= 'Z') buff = (buff + (aux - 'A' + 10)) * 16;
//         else if (aux >= 'a' and aux <= 'z') buff = (buff + (aux - 'a' + 10)) * 16;
//         aux = data[i + 1];
//         if (aux >= '0' and aux <= '9') buff = (buff + (aux - '0'));
//         else if (aux >= 'A' and aux <= 'Z') buff = (buff + (aux - 'A' + 10));
//         else if (aux >= 'a' and aux <= 'z') buff = (buff + (aux - 'a' + 10));
//         result[j] = buff;
//         j++;
//     }
// }

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

String Helper::uint8ArrayToHexString(uint8_t* array, uint8_t length) {
    String str;
    for (int i = 0; i < length; i++) {
        String aux = String(array[i], HEX);
        if (aux.length() == 1) aux = "0" + aux;
        str = str + aux;
    }
    return str;
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

void Helper::utf8ToByteArray(String data, uint8_t* array) {
    int j = 0;
    for (int i = 0; i < data.length(); i++) {
        if (data[i] == '\\') {
            if (data[i + 1] == 'x') {
                String aux = data.substring(i + 2, i + 4);
                array[j] = (uint8_t) strtol(aux.c_str(), NULL, 16);
                j++;
                i = i + 3;
            }
        }
        else {
            array[j] = data[i];
            j++;
        }
    }
}

void Helper::hex2bin(const char* src, char* target) {
    // while (*src && src[1]) {
    //     *target++ = char2int(*src) << 4 | char2int(src[1]);
    //     src += 2;
    // }
    while (*src && src[1]) {
        *(target++) = char2int(*src) * 16 + char2int(src[1]);
        src += 2;
    }
}

int Helper::char2int(char src) {
    if (src >= '0' && src <= '9') return src - '0';
    if (src >= 'a' && src <= 'f') return src - 'a' + 10;
    if (src >= 'A' && src <= 'F') return src - 'A' + 10;
    return 0;
}

void Helper::printHex(uint8_t* data, int length, String title) {
    Serial.println(title + ": [");
    for (int i = 0; i < length; i++) {
        if (i != 0 && i % 6 == 0) Serial.println();
        Serial.print(String(data[i]) + (length - 1 != i ? ", " : ""));
    }
    Serial.println("]");
}
