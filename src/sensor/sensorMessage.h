#pragma once

#include <Arduino.h>

#include <ArduinoLog.h>

#include "message/dataMessage.h"

#pragma pack(1)

enum DataSensorType: uint8_t {
    SensorTemperature = 0,
    SensorDht22 = 1
};

// TODO: SensorMessageType generic for all sensors, we can add more types later. Not implemented yet. 
// TODO: This is an idea to make it generic and easy to send through LoRaMesher.
enum SensorMessageType: uint8_t {
    Start = 0,
    Pause = 1,
    SetInterval = 2,
    GetValue = 3,
};

class SensorMessageGeneric: public DataMessageGeneric {
public:
    DataSensorType sensorType;
    SensorMessageType messageType;

    void serialize(JsonObject& doc) {
        // Call the base class serialize function
        ((DataMessageGeneric*) (this))->serialize(doc);

        // Add the derived class data to the JSON object
        // doc["sensorType"] = sensorType;
    }
};

class SensorMessage: public SensorMessageGeneric {
    uint8_t payload[];
};

#pragma pack()