#pragma once

#include <Arduino.h>
#include "message/dataMessage.h"

#define MONCOUNT_MONONEMESSAGE UINT16_MAX

#pragma pack(1)
class monMessage : public DataMessageGeneric {
    // see LoRaMesher/src/entities/routingTable/RouteNode.h
public:
    uint16_t RTcount = 0;
    uint16_t address = 0;
    uint16_t via = 0;
    uint8_t metric = 0;
    int8_t receivedSNR = 0;
    int8_t sentSNR = 0;
    unsigned long SRTT = 0;
    unsigned long RTTVAR = 0;
    void serialize(JsonObject& doc) {
        // Call the base class serialize function
        ((DataMessageGeneric*)(this))->serialize(doc);
        // Add the derived class data to the JSON object
        doc["RTcount"] = RTcount;
        doc["address"] = address;
        doc["via"] = via;
        doc["metric"] = metric;
        doc["receivedSNR"] = receivedSNR;
        doc["sentSNR"] = sentSNR;
        doc["SRTT"] = SRTT;
        doc["RTTVAR"] = RTTVAR;
    }
    void deserialize(JsonObject& doc) {
        // Call the base class deserialize function
        ((DataMessageGeneric*)(this))->deserialize(doc);
        // Add the derived class data to the JSON object
        RTcount = doc["RTcount"];
        address = doc["address"];
        via = doc["via"];
        metric = doc["metric"];
        receivedSNR = doc["receivedSNR"];
        sentSNR = doc["sentSNR"];
        SRTT = doc["SRTT"];
        RTTVAR = doc["RTTVAR"];
    }
};

struct routing_entry {
    uint16_t neighbor = 0;
    uint16_t next_hop = 0;
    uint8_t link_quality = 0;
    uint8_t hop_count = 0;
};

class monOneMessage : public DataMessageGeneric {
    // see LoRaMesher/src/entities/routingTable/RouteNode.h
public:
    uint16_t RTcount = MONCOUNT_MONONEMESSAGE;  // for backward compatibility
    unsigned long uptime;
    uint16_t TxQ;
    uint16_t RxQ;
    uint32_t number_of_neighbors;
    routing_entry rt[];
    void operator delete(void* ptr) {
        ESP_LOGI("monOneMessage", "delete");
        vPortFree(ptr);
    };
    void serialize(JsonObject& doc) {
        // Call the base class serialize function
        ((DataMessageGeneric*)(this))->serialize(doc);
        // Add the derived class data to the JSON object
        doc["RTcount"] = MONCOUNT_MONONEMESSAGE;
        doc["uptime"] = uptime;
        doc["TxQ"] = TxQ;
        doc["RxQ"] = RxQ;
        doc["number_of_neighbors"] = number_of_neighbors;
        JsonArray rtArray = doc.createNestedArray("rt");
        for (int i = 0; i < number_of_neighbors; i++) {
            rtArray[i]["neighbor"] = rt[i].neighbor;
            rtArray[i]["next_hop"] = rt[i].next_hop;
            rtArray[i]["link_quality"] = rt[i].link_quality;
            rtArray[i]["hop_count"] = rt[i].hop_count;
        }
    }
    void deserialize(JsonObject& doc) {
        // Call the base class deserialize function
        ((DataMessageGeneric*)(this))->deserialize(doc);
        // Add the derived class data to the JSON object
        RTcount = doc["RTcount"];
        uptime = doc["uptime"];
        TxQ = doc["TxQ"];
        RxQ = doc["RxQ"];
        number_of_neighbors = doc["number_of_neighbors"];
        for (int i = 0; i < number_of_neighbors; i++) {
            rt[i].neighbor = doc["rt"][i]["neighbor"];
            rt[i].next_hop = doc["rt"][i]["next_hop"];
            rt[i].link_quality = doc["rt"][i]["link_quality"];
            rt[i].hop_count = doc["rt"][i]["hop_count"];
        }
    }
};

#pragma pack()
