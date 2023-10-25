#pragma once

#include <Arduino.h>

#include "SHT4xAirSensorMessage.h"

// #include <SensirionI2CSht4x.h>

class SHT4xAirSensor {
public:
    void init();

    SHT4xAirSensorMessage read();

private:
    // SensirionI2CSht4x sht4x;

    bool initialized = false;
};

