#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#include "gps/gpsMessage.h"

#include "sensor/metadata/metadataSensorMessage.h"

#pragma pack(1)
class MetadataMessage final: public DataMessageGeneric {
public:
    MetadataMessage() {
    }

    GPSMessage gps;
    int metadataSendTimeInterval;
    float batteryPercentage;
    // uint8_t metadataSize;
    // MetadataSensorMessage sensorMetadata[];

    void serialize(JsonObject& doc) {
        // Call the base class serialize function
        ((DataMessageGeneric*) (this))->serialize(doc);

        // Add the GPS data to the JSON object
        gps.serialize(doc);

        doc["message_type"] = "metadata";

        // Add the derived class data to the JSON object
        doc["metadata_send_time_interval"] = metadataSendTimeInterval;

        // Add the battery percentage to the JSON object
        doc["battery_percentage"] = batteryPercentage;

        doc.createNestedArray("message");

        // JsonArray sensorsArray = doc.createNestedArray("message");

        // for (int i = 0; i < metadataSize; i++) {
        //     JsonObject sensorObject = sensorsArray.createNestedObject();
        //     sensorMetadata[i].serialize(sensorObject);
        //     // TODO: Add the sensor id to the metadata message
        //     sensorObject["id"] = i;
        // }
    }
};

#pragma pack()
