#pragma once

#include <Arduino.h>

#include <ArduinoJson.h>

#include <cstdlib>
#include <memory>

#pragma pack(1)

// Message Ports
enum messagePort : uint8_t {
    LoRaMeshPort = 1,
    BluetoothPort = 2,
    WiFiPort = 3,
    MqttPort = 4,
    InternalPort = 5,
};

// TODO: This should be defined by the user, all the apps that are available and their numbers
// should be the same
// TODO: in all the nodes of the network.
enum appPort : uint8_t {
    LoRaChat = 1,
    BluetoothApp = 2,
    WiFiApp = 3,
    GPSApp = 4,
    WalletApp = 5,
    CommandApp = 6,
    LoRaMesherApp = 7,
    MQTTApp = 8,
    SimApp = 12,
    LedApp = 13,
    SensorApp = 14,
    MetadataApp = 15,
    MonApp = 16,
    DisplayApp = 17,
    OTAApp = 18,
    NodeApp = 19,
};

class DataMessageGeneric {
public:
    appPort appPortDst;
    appPort appPortSrc;
    uint8_t messageId;

    uint16_t addrSrc;
    uint16_t addrDst;

    uint32_t messageSize;  // Message Size of the payload no include header

    uint32_t getDataMessageSize() { return sizeof(DataMessageGeneric) + messageSize; }

    void serialize(JsonObject& doc) {
        // doc["appPortDst"] = appPortDst;
        // doc["appPortSrc"] = appPortSrc;
        doc["messageId"] = messageId;
        doc["addrSrc"] = addrSrc;
        doc["addrDst"] = addrDst;
        doc["messageSize"] = messageSize;
    }

    void deserialize(JsonObject& doc) {
        appPortDst = (appPort)doc["appPortDst"];
        appPortSrc = (appPort)doc["appPortSrc"];
        messageId = doc["messageId"];
        addrSrc = doc["addrSrc"];
        addrDst = doc["addrDst"];
        messageSize = doc["messageSize"];
    }
};

class DataMessage : public DataMessageGeneric {
public:
    uint8_t message[];
};

#pragma pack()

/** Frees a message made by allocateDataMessage(). */
struct DataMessageDeleter {
    void operator()(DataMessage* message) const {
        // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): calloc block
        free(message);
    }
};

/** Sole owner of a message and its payload. */
using DataMessagePtr = std::unique_ptr<DataMessage, DataMessageDeleter>;

/**
 * @brief Allocates a zeroed message with room for @p payloadSize payload bytes.
 *
 * Every DataMessage, and every message type that extends DataMessageGeneric, is allocated here:
 * the payload is a flexible array member, so the size is only known at run time.
 * @return the message with messageSize set, or nullptr if the heap is exhausted.
 */
inline DataMessagePtr allocateDataMessage(size_t payloadSize) {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc): flexible array member, freed by the deleter
    DataMessagePtr message(static_cast<DataMessage*>(calloc(1, sizeof(DataMessage) + payloadSize)));
    if (message) message->messageSize = static_cast<uint32_t>(payloadSize);
    return message;
}
