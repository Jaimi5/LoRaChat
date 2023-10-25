#include "SoilHTSensor.h"

static const char* SOIL_SENSOR_TAG = "SoilSensor";


void SoilHTSensor::init() {
    ESP_LOGV(SOIL_SENSOR_TAG, "Initializing Soil Temperature and Moisture Sensor");

    initialized = true;
}

SoilSensorMessage SoilHTSensor::read() {
    return SoilSensorMessage(-1, -1, -1);

    // if (!initialized) {
    //     ESP_LOGE(SOIL_SENSOR_TAG, "Soil Temperature and Moisture Sensor not initialized");
    //     return SoilSensorMessage(-1, -1, -1);
    // }

    // int16_t temperature = -1;
    // int16_t moisture = -1;
    // int16_t conductivity = -1;

    // byte scratchpad[9];

    // //Send RESET
    // if (!ds.reset()) {
    //     ESP_LOGE(SOIL_SENSOR_TAG, "No sensor found");
    //     return SoilSensorMessage(-1, -1, -1);
    // }

    // ds.skip();//Send ROM Command-Skip ROM
    // ds.write(0x44);//Send Function command- convert T

    // ESP_LOGV(SOIL_SENSOR_TAG, "Waiting for conversion to complete");
    // while (ds.read_bit() == 0) {
    //     vTaskDelay(10 / portTICK_PERIOD_MS);
    // }
    // ESP_LOGV(SOIL_SENSOR_TAG, "Conversion complete");

    // ds.reset();//Send RESET
    // ds.skip();//Send ROM Command-Skip ROM
    // ds.write(0xBE);//Send Function command- Read Scratchpad
    // for (int i = 0; i < 9; i++) {// we need 9 bytes
    //     scratchpad[i] = ds.read();
    //     Serial.printf("%.2X ", scratchpad[i]);
    // }

    // if (OneWire::crc8(scratchpad, 8) == scratchpad[8]) {
    //     temperature = makeWord(scratchpad[0], scratchpad[1]) / 100;
    //     moisture = makeWord(scratchpad[2], scratchpad[3]) / 100;
    //     conductivity = makeWord(scratchpad[4], scratchpad[5]);

    //     ESP_LOGV(SOIL_SENSOR_TAG, "Temperature: %.2f, Moisture: %.2f, Conductivity: %d", temperature, moisture, conductivity);
    // }
    // else
    //     ESP_LOGE(SOIL_SENSOR_TAG, "CRC Error");

    // return SoilSensorMessage(temperature, moisture, conductivity);
}

