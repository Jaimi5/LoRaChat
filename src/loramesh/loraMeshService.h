#pragma once

#include <Arduino.h>

#include "LoRaMesher.h"

#include "ArduinoLog.h"

#include "loraMeshMessage.h"

#include "./message/messageManager.h"

//TODO:?
// #include "lorameshCommandService.h"

class LoRaMeshService {

public:

    /**
     * @brief Construct a new LoRaMeshService object
     *
     */
    static LoRaMeshService& getInstance() {
        static LoRaMeshService instance;
        return instance;
    }

    void initLoraMesherService();

    uint16_t getDeviceID();

    void loopReceivedPackets();

    String getRoutingTable();

    void sendReliable(DataMessage* message);

private:

    LoraMesher& radio = LoraMesher::getInstance();

    TaskHandle_t receiveLoRaMessage_Handle = NULL;

    LoRaMeshService() {};

    void createReceiveMessages();

    LoRaMeshMessage* createLoRaMeshMessage(DataMessage* message);

    DataMessage* createDataMessage(AppPacket<LoRaMeshMessage>* message);
};

