#include "SHT4xAirSensor.h"

static const char* AIR_SENSOR_TAG = "SHT4x";

void SHT4xAirSensor::init() {
    ESP_LOGV(AIR_SENSOR_TAG, "Initializing Temperature and Humidity Sensor");

    initialized = false;

    return;

    // sht4x.begin(Wire);

    // uint16_t error;
    // char errorMessage[256];

    // uint32_t serialNumber;
    // error = sht4x.serialNumber(serialNumber);
    // if (error) {
    //     errorToString(error, errorMessage, 256);
    //     ESP_LOGE(AIR_SENSOR_TAG, "Error trying to execute serialNumber(): %s", errorMessage);
    //     return;
    // }

    // ESP_LOGV(AIR_SENSOR_TAG, "Serial Number: %d", serialNumber);
    // ESP_LOGV(AIR_SENSOR_TAG, "Air Sensor initialized");
    // initialized = true;
}

SHT4xAirSensorMessage SHT4xAirSensor::read() {
    return SHT4xAirSensorMessage(-1, -1);

    // if (!initialized) {
    //     ESP_LOGE(AIR_SENSOR_TAG, "Air Sensor not initialized");
    //     return SHT4xAirSensorMessage(-1, -1);
    // }

    // uint16_t error;
    // char errorMessage[256];

    // float temp = 0;
    // float humidity = 0;

    // error = sht4x.measureHighPrecision(temp, humidity);
    // if (error) {
    //     errorToString(error, errorMessage, 256);
    //     ESP_LOGE(AIR_SENSOR_TAG, "Error trying to execute measureHighPrecision(): %s", errorMessage);
    // }
    // else
    //     ESP_LOGI(AIR_SENSOR_TAG, "Temperature: %f, Humidity: %f", temp, humidity);

    // return SHT4xAirSensorMessage(temp, humidity);
}

