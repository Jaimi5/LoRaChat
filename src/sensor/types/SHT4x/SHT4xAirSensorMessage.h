#pragma once

#include <Arduino.h>

#include <ArduinoJson.h>

#pragma pack(1)
class SHT4xAirSensorMessage {
public:
    float temperature;
    float humidity;

    SHT4xAirSensorMessage() {}

    SHT4xAirSensorMessage(float temperature, float humidity): temperature(temperature), humidity(humidity) {}

    void serialize(JsonArray& doc) {
        JsonObject humObj = doc.createNestedObject();
        humObj["measurement"] = humidity;
        humObj["type"] = "humidity";

        JsonObject tempObj = doc.createNestedObject();
        tempObj["measurement"] = temperature;
        tempObj["type"] = "temperature";
    }
};
#pragma pack()