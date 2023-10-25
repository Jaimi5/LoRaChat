#pragma once

#include <Arduino.h>

#include <ArduinoJson.h>

#pragma pack(1)
class WaterLevelSensorMessage {
public:
    float distance;

    WaterLevelSensorMessage() {}

    WaterLevelSensorMessage(float distance): distance(distance) {}

    void serialize(JsonArray& doc) {
        JsonObject distObj = doc.createNestedObject();
        distObj["measurement"] = distance;
        distObj["type"] = "Water_Level";
    }
};
#pragma pack()