// #pragma once

// #include <Arduino.h>

// #include "config.h"

// #include "message/messageManager.h"

// #include "message/messageService.h"

// #include "gps/gpsService.h"

// #include "sensor/metadata/metadata.h"


// template <typename T>
// class Sensor: public MessageService {
// protected:
//     uint32_t readEveryMs = 0;

//     T previousValues[STORED_SENSOR_DATA];

//     uint8_t previousValuesIndex = 0;

//     uint16_t readValues = 0;

//     uint8_t sensorMessageId = 0;

// public:
//     Sensor(uint8_t id, String name, uint32_t readEveryMs): MessageService(id, name) {
//         this->readEveryMs = readEveryMs;

//         resetPreviousValues();
//     };

//     virtual T readValue() = 0;
//     virtual T readValueWait(uint8_t retries) = 0;

//     float getVariance() {
//         float mean = 0;
//         uint8_t size = readValues > STORED_SENSOR_DATA ? STORED_SENSOR_DATA : readValues;

//         if (size == 0)
//             return 0;

//         for (int i = 0; i < size; i++) {
//             mean += previousValues[i];
//         }
//         mean /= size;

//         float variance = 0;
//         for (int i = 0; i < size; i++) {
//             variance += pow(previousValues[i] - mean, 2);
//         }
//         variance /= size;

//         return variance;
//     };

//     float getStandardDeviation() {
//         float variance = getVariance();

//         if (variance == 0)
//             return 0;

//         float standardDeviation = sqrt(variance);

//         return standardDeviation;
//     };

//     MetadataSensorMessage* getMetadataMessage() {
//         metadataMessage->type = serviceId;
//         metadataMessage->variance = getVariance();
//         metadataMessage->stDev = getStandardDeviation();
//         metadataMessage->sampleSize = readValues > STORED_SENSOR_DATA ? STORED_SENSOR_DATA : readValues;
//         metadataMessage->sendTimeInterval = readEveryMs;
//         metadataMessage->calibration = 0;
//         metadataMessage->missingValues = 0;

//         return metadataMessage;
//     };

// protected:
//     void resetPreviousValues() {
//         for (int i = 0; i < STORED_SENSOR_DATA; i++) {
//             previousValues[i] = 0;
//         }
//     };


//     MetadataSensorMessage* metadataMessage = new MetadataSensorMessage();

// };