// #pragma once

// #include <Arduino.h>

// #include "message/dataMessage.h"

// #include "gps/gpsMessage.h"

// #include "signatureMessage.h"

// #pragma pack(1)

// enum DataSensorType: uint8_t {
//     SensorTemperature = 0,
//     SensorSoil = 1,
//     SensorGround = 2,
// };

// enum SensorMessageType: uint8_t {
//     GetValue = 0
// };

// class SensorMessageGeneric: public DataMessageGeneric {//}, public SignatureMessage {
// public:
//     GPSMessage gps;
//     DataSensorType sensorType;

//     void serialize(JsonObject& doc) {
//         // Call the base class serialize function
//         ((DataMessageGeneric*) (this))->serialize(doc);

//         // Add the GPS data to the JSON object
//         gps.serialize(doc);

//         doc["message_type"] = "measurement";

//         // Add the derived class data to the JSON object
//         // doc["sensorType"] = sensorType;
//     }
// };

// class SensorMessage: public SensorMessageGeneric {
//     uint8_t payload[];
// };

// #pragma pack()