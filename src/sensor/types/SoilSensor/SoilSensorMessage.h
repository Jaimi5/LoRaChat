#pragma once

#include <Arduino.h>

#include <ArduinoJson.h>

#pragma pack(1)
class SoilSensorMessage {
public:
    int16_t temperature = 0;
    int16_t moisture = 0;
    int16_t conductivity = 0;

    SoilSensorMessage() {}

    SoilSensorMessage(int16_t temperature, int16_t moisture, int16_t conductivity): temperature(temperature), moisture(moisture), conductivity(conductivity) {}

    void serialize(JsonArray& doc) {
        JsonObject tempObj = doc.createNestedObject();
        tempObj["measurement"] = temperature;
        tempObj["type"] = "Soil_Temperature_Low_Res";

        JsonObject humObj = doc.createNestedObject();
        humObj["measurement"] = moisture;
        humObj["type"] = "Soil_Moisture";

        JsonObject condObj = doc.createNestedObject();
        condObj["measurement"] = conductivity;
        condObj["type"] = "Soil_Conductivity";
    }
};
#pragma pack()