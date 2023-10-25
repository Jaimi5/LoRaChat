#include "battery.h"

static const char* BATTERY_TAG = "Battery";

void Battery::init() {
#if defined(BATTERY_ENABLED)
    ESP_LOGI(BATTERY_TAG, "Initializing battery");

    pinMode(BATTERY_PIN, INPUT);

    ESP_LOGI(BATTERY_TAG, "Battery initialized");
#endif

}

float Battery::getVoltagePercentage() {
#if defined(BATTERY_ENABLED)
    float voltage = (float) (analogRead(BATTERY_PIN)) / 4095 * 2 * 3.3 * 1.1;
    return voltage;
#else
    return 0;
#endif
}
