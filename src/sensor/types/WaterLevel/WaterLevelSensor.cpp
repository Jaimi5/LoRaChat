#include "WaterLevelSensor.h"

static const char* WATER_LEVEL_SENSOR_TAG = "WaterLevelSensor";

// bool isSensorConnected(int sda, int scl) {
//     Wire.begin(sda, scl);
//     Wire.beginTransmission(0x29);
//     return Wire.endTransmission() == 0;
// }

void WaterLevelSensor::init() {
    ESP_LOGV(WATER_LEVEL_SENSOR_TAG, "Initializing Water Level Sensor");

    initialized = false;

    return;

    // // TODO: Check if this is the correct way to check if the sensor is connected
    // if (!isSensorConnected(SDA, SCL)) {
    //     ESP_LOGE(WATER_LEVEL_SENSOR_TAG, "Water Level Sensor not connected");
    //     return;
    // }

    // if (!vl53.begin(0x29, &Wire, true)) {
    //     ESP_LOGE(WATER_LEVEL_SENSOR_TAG, "Error on init of VL sensor: %d", vl53.vl_status);
    //     return;
    // }

    // ESP_LOGV(WATER_LEVEL_SENSOR_TAG, "Sensor ID: 0x %X", vl53.sensorID());

    // if (!vl53.startRanging()) {
    //     ESP_LOGE(WATER_LEVEL_SENSOR_TAG, "Error on start ranging of VL sensor: %d", vl53.vl_status);
    //     return;
    // }

    // ESP_LOGV(WATER_LEVEL_SENSOR_TAG, "Ranging started");

    // // Valid timing budgets: 15, 20, 33, 50, 100, 200 and 500ms!
    // if (!vl53.setTimingBudget(500)) {
    //     ESP_LOGE(WATER_LEVEL_SENSOR_TAG, "Error on set timing budget of VL sensor: %d", vl53.vl_status);
    //     return;
    // }

    // ESP_LOGV(WATER_LEVEL_SENSOR_TAG, "Timing budget set to %dms", vl53.getTimingBudget());

    // ESP_LOGV(WATER_LEVEL_SENSOR_TAG, "Water Level Sensor initialized");

    // initialized = true;

    // vl53.stopRanging();
}

WaterLevelSensorMessage WaterLevelSensor::read() {
    return WaterLevelSensorMessage(-1);

    // if (!initialized) {
    //     ESP_LOGE(WATER_LEVEL_SENSOR_TAG, "Water Level Sensor not initialized");
    //     return WaterLevelSensorMessage(-1);
    // }

    // vl53.startRanging();

    // vTaskDelay(600 / portTICK_PERIOD_MS);

    // float distance = -1;
    // if (vl53.dataReady()) {
    //     // new measurement for the taking!
    //     distance = vl53.distance();
    //     if (distance == -1) {
    //         // something went wrong!
    //         ESP_LOGE(WATER_LEVEL_SENSOR_TAG, "Error on read of VL sensor: %d", vl53.vl_status);
    //         return WaterLevelSensorMessage(-1);
    //     }

    //     // The sensor is 400mm above the water level so we need to invert the distance
    //     distance = (distance - 400) * -1;

    //     ESP_LOGI(WATER_LEVEL_SENSOR_TAG, "Distance: %f mm", distance);
    //     // data is read out, time for another reading!
    //     vl53.clearInterrupt();
    // }

    // vl53.stopRanging();

    // return WaterLevelSensorMessage(distance);
}
