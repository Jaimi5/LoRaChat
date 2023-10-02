#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#include "LoraMesher.h"

#include "config.h"

#pragma pack(1)

enum SimCommand: uint8_t {
    StartSim = 0,
    StopSim = 1,
    Message = 2,
    Payload = 3,
    StartingSimulation = 4,
    EndedSimulation = 5, // StartedSimulationStatus
    EndedSimulationStatus = 6,
};


class SimMessageState {
public:
    LM_State state;

    void serializeState(JsonObject& doc) {
        JsonObject data = doc.createNestedObject("state");

        data["Id"] = state.id;
        data["Type"] = state.type;
        data["QR"] = state.receivedQueueSize;
        data["QS"] = state.sentQueueSize;
        data["QRU"] = state.receivedUserQueueSize;
        data["QWRP"] = state.q_WRPSize;
        data["QWSP"] = state.q_WSPSize;
        data["RT"] = state.routingTableSize;
        data["SSS"] = state.secondsSinceStart;
        data["FMA"] = state.freeMemoryAllocation;

        JsonObject packetHeader = data.createNestedObject("packetHeader");

        this->serializePacketHeader(packetHeader);
    }

    void serializePacketHeader(JsonObject& doc) {
        doc["Type"] = state.packetHeader.type;
        doc["Id"] = state.packetHeader.id;
        doc["Size"] = state.packetHeader.packetSize;
        doc["Src"] = state.packetHeader.src;
        doc["Dst"] = state.packetHeader.dst;
        doc["Via"] = state.packetHeader.via;
        doc["SeqId"] = state.packetHeader.seq_id;
        doc["Num"] = state.packetHeader.number;
    }
};

class SimPayloadMessage {
public:
    uint32_t packetSize;

    uint8_t payload[];

    void serializePayload(JsonObject& doc) {
        doc["packetSize"] = packetSize;

        if (UPLOAD_PAYLOAD == true) {
            JsonArray payloadArray = doc.createNestedArray("payload");

            for (uint32_t i = 0; i < packetSize; i++) {
                payloadArray.add(payload[i]);
            }
        }
        else {
            doc["payload"] = payload[packetSize - 1];
        }
    }
};

class SimMessage: public DataMessageGeneric {
public:
    SimCommand simCommand;

    uint8_t payload[];

    void serialize(JsonObject& doc) {
        // Call the base class serialize function
        ((DataMessageGeneric*) (this))->serialize(doc);

        // Add the derived class data to the JSON object
        doc["simCommand"] = simCommand;

        switch (simCommand) {
            case::SimCommand::Message:
                {
                    SimMessageState* message = (SimMessageState*) this->payload;
                    message->serializeState(doc);
                    break;
                }
            case::SimCommand::Payload:
                {
                    SimPayloadMessage* payloadMessage = (SimPayloadMessage*) this->payload;
                    payloadMessage->serializePayload(doc);
                    break;
                }
            default:
                break;
        }
    }

    void deserialize(JsonObject& doc) {
        // Call the base class deserialize function
        ((DataMessageGeneric*) (this))->deserialize(doc);

        // Add the derived class data to the JSON object
        simCommand = (SimCommand) doc["simCommand"];
    }
};

#pragma pack()