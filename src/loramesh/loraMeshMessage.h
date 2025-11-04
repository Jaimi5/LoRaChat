#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#include "LoraMesher.h"

#pragma pack(1)

enum LoRaMeshMessageType : uint8_t {
    sendMessage = 1,
    getRoutingTable = 2,
    routingTableMessage = 3,
};

enum NodeRole : uint8_t {
    Default = 0,
    Gateway = 1,
};

class LoRaMeshMessage {
public:
    appPort appPortDst;
    appPort appPortSrc;
    uint8_t messageId;
    uint8_t dataMessage[];
};

struct RoutingTableEntry {
    uint16_t address;
    uint16_t via;
    uint8_t reverseETX;  // Scaled by 10 (e.g., 10 = ETX 1.0, 20 = ETX 2.0)
    uint8_t forwardETX;  // Scaled by 10 (e.g., 10 = ETX 1.0, 20 = ETX 2.0)
    uint8_t role;        // 0 = Default, 1 = Gateway
};

class RoutingTableMessage : public DataMessageGeneric {
public:
    uint32_t numberOfEntries;
    RoutingTableEntry entries[];

    void operator delete(void* ptr) {
        vPortFree(ptr);
    }

    void serialize(JsonObject& doc) {
        // Call the base class serialize function
        ((DataMessageGeneric*)(this))->serialize(doc);

        // Add the derived class data to the JSON object
        doc["numberOfEntries"] = numberOfEntries;
        JsonArray entriesArray = doc.createNestedArray("entries");
        for (uint32_t i = 0; i < numberOfEntries; i++) {
            JsonObject entry = entriesArray.createNestedObject();
            entry["address"] = entries[i].address;
            entry["via"] = entries[i].via;
            entry["reverseETX"] = entries[i].reverseETX;
            entry["forwardETX"] = entries[i].forwardETX;
            entry["role"] = entries[i].role;
        }
    }

    void deserialize(JsonObject& doc) {
        // Call the base class deserialize function
        ((DataMessageGeneric*)(this))->deserialize(doc);

        // Add the derived class data from the JSON object
        numberOfEntries = doc["numberOfEntries"];
        JsonArray entriesArray = doc["entries"];
        for (uint32_t i = 0; i < numberOfEntries; i++) {
            entries[i].address = entriesArray[i]["address"];
            entries[i].via = entriesArray[i]["via"];
            entries[i].reverseETX = entriesArray[i]["reverseETX"];
            entries[i].forwardETX = entriesArray[i]["forwardETX"];
            entries[i].role = entriesArray[i]["role"];
        }
    }
};
#pragma pack()
