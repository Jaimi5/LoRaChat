#pragma once

#include <Arduino.h>

// #include "Adafruit_VL53L1X.h"

#include "WaterLevelSensorMessage.h"

class WaterLevelSensor {
public:
    void init();

    WaterLevelSensorMessage read();

private:
    // Adafruit_VL53L1X vl53 = Adafruit_VL53L1X();

    bool initialized = false;
};

