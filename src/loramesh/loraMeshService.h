#pragma once

#include <Arduino.h>

#include "LoRaMesher.h"

#include "ArduinoLog.h"

#include "./message/messageService.h"

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

    String sendReliable(uint16_t destination, String message);

private:

    LoraMesher& radio = LoraMesher::getInstance();

    TaskHandle_t receiveLoRaMessage_Handle = NULL;

    LoRaMeshService() {};

    void createReceiveMessages();

};

