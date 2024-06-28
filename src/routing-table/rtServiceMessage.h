#pragma once

#include <Arduino.h>

#include "message/dataMessage.h"

#pragma pack(1)
class rtMessage : public DataMessageGeneric {
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
  void serialize(JsonObject &doc) {
    // Call the base class serialize function
    ((DataMessageGeneric *)(this))->serialize(doc);
    // Add the derived class data to the JSON object
    doc["RTcount"] = RTcount ;
    doc["address"] = address;
    doc["via"] = via;
    doc["metric"] = metric;
    doc["receivedSNR"] = receivedSNR;
    doc["sentSNR"] = sentSNR;
    doc["SRTT"] = SRTT;
    doc["RTTVAR"] = RTTVAR;
  }
  void deserialize(JsonObject &doc) {
    // Call the base class deserialize function
    ((DataMessageGeneric *)(this))->deserialize(doc);
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

#pragma pack()
