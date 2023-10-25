#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#include "gps/gpsMessage.h"

#include "types/PHSensor/PHSensorMessage.h"

#include "types/SHT4x/SHT4xAirSensorMessage.h"

#include "types/SoilSensor/SoilSensorMessage.h"

#include "types/WaterLevel/WaterLevelSensorMessage.h"

#pragma pack(1)

enum SensorCommand: uint8_t {
    Data = 0,
    Calibrate = 1,
};


class MeasurementMessage: public DataMessageGeneric {
public:
    SensorCommand sensorCommand;

    GPSMessage gps;

    PHSensorMessage phSensorMessage;

    SHT4xAirSensorMessage sht4xAirSensorMessage;

    SoilSensorMessage soilSensorMessage;

    WaterLevelSensorMessage waterLevelSensorMessage;

    void serialize(JsonObject& doc) {
        // Create a data object
        JsonObject dataObj = doc.createNestedObject("data");

        // Add the data to the data object
        serializeDataSerialize(dataObj);
    }

    void serializeDataSerialize(JsonObject& doc) {
        // Call the base class serialize function
        ((DataMessageGeneric*) (this))->serialize(doc);

        // Add the GPS data to the JSON object
        gps.serialize(doc);

        // Add that is a measurement message
        doc["message_type"] = "measurement";

        // Set all the measurements in an array called message
        JsonArray measurements = doc.createNestedArray("message");

        // Add the PH sensor data to the JSON object
        phSensorMessage.serialize(measurements);

        // Add the SHT4x air sensor data to the JSON object
        sht4xAirSensorMessage.serialize(measurements);

        // Add the soil sensor data to the JSON object
        soilSensorMessage.serialize(measurements);

        // Add the water level sensor data to the JSON object
        waterLevelSensorMessage.serialize(measurements);
    }

    void deserialize(JsonObject& doc) {
        // Call the base class serialize function
        ((DataMessageGeneric*) (this))->deserialize(doc);
    }
};

class SensorCommandMessage: public DataMessageGeneric {
public:
    SensorCommand sensorCommand;
    uint8_t payload[];

    void serialize(JsonObject& doc) {
        switch (sensorCommand) {
            case SensorCommand::Data:
                ((MeasurementMessage*) (this))->serialize(doc);
                break;
            case SensorCommand::Calibrate:
                // Call the base class serialize function
                ((DataMessageGeneric*) (this))->serialize(doc);
                doc["sensorCommand"] = sensorCommand;
                break;
        }
    }

    void deserialize(JsonObject& doc) {
        switch ((SensorCommand) doc["sensorCommand"]) {
            case SensorCommand::Data:
                break;
            case SensorCommand::Calibrate:
                // Call the base class deserialize function
                ((DataMessageGeneric*) (this))->deserialize(doc);
                break;
        }

        // Add the derived class data to the JSON object
        sensorCommand = doc["sensorCommand"];
    }
};


#pragma pack()