#pragma once

#include <Arduino.h>

#include <ArduinoJson.h>


#pragma pack(1)

//Message Ports
enum messagePort: uint8_t {
    LoRaMeshPort = 1,
    BluetoothPort = 2,
    WiFiPort = 3,
    MqttPort = 4
};

//TODO: This should be defined by the user, all the apps that are available and their numbers should be the same
//TODO: in all the nodes of the network.
enum appPort: uint8_t {
    LoRaChat = 1,
    BluetoothApp = 2,
    WiFiApp = 3,
    GPSApp = 4,
    SOSApp = 5,
    CommandApp = 6,
    LoRaMesherApp = 7,
    MQTTApp = 8,
    TemperatureSensorApp = 9,
    LedApp = 10,
    Dht22SensorApp = 11,
    SimApp = 12
};

class DataMessageGeneric {
public:
    appPort appPortDst;
    appPort appPortSrc;
    uint8_t messageId;

    uint16_t addrSrc;
    uint16_t addrDst;

    uint32_t messageSize; //Message Size of the payload no include header

    uint32_t getDataMessageSize() {
        return sizeof(DataMessageGeneric) + messageSize;
    }

    void serialize(JsonObject& doc) {
        doc["appPortDst"] = appPortDst;
        doc["appPortSrc"] = appPortSrc;
        doc["messageId"] = messageId;
        doc["addrSrc"] = addrSrc;
        doc["addrDst"] = addrDst;
        doc["messageSize"] = messageSize;
    }

    void deserialize(JsonObject& doc) {
        appPortDst = (appPort) doc["appPortDst"];
        appPortSrc = (appPort) doc["appPortSrc"];
        messageId = doc["messageId"];
        addrSrc = doc["addrSrc"];
        addrDst = doc["addrDst"];
        messageSize = doc["messageSize"];
    }
};

class DataMessage: public DataMessageGeneric {
public:
    uint8_t message[];
};

#pragma pack()