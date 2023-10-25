#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#pragma pack(1)

enum LedCommand: uint8_t {
    Off = 0,
    On = 1
};

class LedMessage: public DataMessageGeneric {
public:
    LedCommand ledCommand;

    void serialize(JsonObject& doc) {
        // Call the base class serialize function
        ((DataMessageGeneric*) (this))->serialize(doc);

        // Add the derived class data to the JSON object
        doc["ledCommand"] = ledCommand;
    }

    void deserialize(JsonObject& doc) {
        // Call the base class deserialize function
        ((DataMessageGeneric*) (this))->deserialize(doc);

        // Add the derived class data to the JSON object
        ledCommand = doc["ledCommand"];
    }
};
#pragma pack()