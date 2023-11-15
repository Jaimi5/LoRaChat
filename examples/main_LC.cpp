#include <Arduino.h>
#include "LoRaChat.cpp"
#include "led/led.h"

Led& led = Led::getInstance();
LC lc;

void setup() {
    Serial.begin(115200);
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);

    lc.init("WIFI_SSID", "WIFI_PASSWORD", "MQTT_SERVER", 1883, "MQTT_USERNAME", "MQTT_PASSWORD");
    lc.registerApplication(&led);
    lc.printCommands();

    led.init();
}

void loop() {
    delay(100);
}
