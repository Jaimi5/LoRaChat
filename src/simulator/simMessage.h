#pragma once

#include <Arduino.h>

#include <ArduinoLog.h>

#include "message/dataMessage.h"

#include "LoraMesher.h"

#pragma pack(1)

enum SimCommand: uint8_t {
    StartSim = 0,
    StopSim = 1,
    Message = 2
};

class SimMessage: public DataMessageGeneric {
public:
    SimCommand simCommand;
    // TODO: Remove state if commands are On/Off
    LM_State state;

    void serialize(JsonObject& doc) {
        // Call the base class serialize function
        ((DataMessageGeneric*) (this))->serialize(doc);

        // Add the derived class data to the JSON object
        doc["simCommand"] = simCommand;

        JsonObject data = doc.createNestedObject("state");

        this->serializeState(data);
    }

    void serializeState(JsonObject& doc) {
        doc["Id"] = state.id;
        doc["Type"] = state.type;
        doc["QR"] = state.receivedQueueSize;
        doc["QS"] = state.sentQueueSize;
        doc["QRU"] = state.receivedUserQueueSize;
        doc["QWRP"] = state.q_WRPSize;
        doc["QWSP"] = state.q_WSPSize;
        doc["RT"] = state.routingTableSize;
        doc["SSS"] = state.secondsSinceStart;
        doc["FMA"] = state.freeMemoryAllocation;

        JsonObject packetHeader = doc.createNestedObject("packetHeader");

        this->serializePacketHeader(packetHeader);
    }

    void serializePacketHeader(JsonObject& doc) {
        doc["Type"] = state.packetHeader.type;
        doc["Id"] = state.packetHeader.id;
        doc["Size"] = state.packetHeader.payloadSize;
        doc["Src"] = state.packetHeader.src;
        doc["Dst"] = state.packetHeader.dst;
        doc["Via"] = state.packetHeader.via;
        doc["SeqId"] = state.packetHeader.seq_id;
        doc["Num"] = state.packetHeader.number;
    }

    void deserialize(JsonObject& doc) {
        // Call the base class deserialize function
        ((DataMessageGeneric*) (this))->deserialize(doc);

        // Add the derived class data to the JSON object
        simCommand = (SimCommand) doc["simCommand"];
    }
};
#pragma pack()