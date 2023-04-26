#pragma once

#include <Arduino.h>

//#include "sensorMessage.h"

#include "sensor/sensorMessage.h"

#pragma pack(1)
class TemperatureMessage: public SensorMessageGeneric {
public:
    float temperature;

    TemperatureMessage(float value) {
        sensorType = SensorTemperature;
        temperature = value;
    }

    void serialize(JsonObject& doc) {
        SensorMessageGeneric* message = (SensorMessageGeneric*) this;
        message->serialize(doc);
        doc["temperature"] = temperature;
    }
};

#pragma pack()