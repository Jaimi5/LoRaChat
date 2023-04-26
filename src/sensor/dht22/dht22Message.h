#pragma once

#include <Arduino.h>

#include "sensor/sensorMessage.h"

#pragma pack(1)
class Dht22Message: public SensorMessageGeneric {
public:
    float temperature;
    float humidity;
    float pression;

    Dht22Message(float temperature, float humidity, float pression) {
        sensorType = SensorDht22;
        temperature = temperature;
        humidity = humidity;
        pression = pression;
    }

    void serialize(JsonObject& doc) {
        SensorMessageGeneric* message = (SensorMessageGeneric*) this;
        message->serialize(doc);
        doc["temperature"] = temperature;
        doc["humidity"] = humidity;
        doc["pression"] = pression;
    }
};

#pragma pack()