#pragma once

#include <Arduino.h>

#include <OneWire.h>

#include "config.h"

#include "SoilSensorMessage.h"

// PIN 12: https://github.com/INFWIN/mt05s-demo/blob/main/sourcecode/MT05S_ArduinoIDE_DOIT_ESP32_DevKit_V1/MT05S_ArduinoIDE_ESP32_DoIt_DevKit_V1.ino


class SoilHTSensor {
public:
    void init();

    SoilSensorMessage read();

private:
    OneWire ds = OneWire(SOIL_SENSOR_PIN);

    bool initialized = false;
};

