#pragma once

#include <Arduino.h>

#include "LoraMesher.h"

#include "loraMeshMessage.h"

#include "message/messageManager.h"

#include "message/messageService.h"

#include "loraMeshCommandService.h"

class LoRaMeshService: public MessageService {

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

    uint16_t getLocalAddress();

    void loopReceivedPackets();

    String getRoutingTable();

    void sendReliable(DataMessage* message);

    bool sendClosestGateway(DataMessage* message);

    static inline void setGateway() {
        LoraMesher::getInstance().addGatewayRole();
    }

    static inline void removeGateway() {
        LoraMesher::getInstance().removeGatewayRole();
    }

    LoRaMeshCommandService* loraMesherCommandService = new LoRaMeshCommandService();

    bool hasActiveConnections();

    bool hasActiveSentConnections();

    bool hasActiveReceivedConnections();

    size_t queueWaitingSendPacketsLength() {
        return radio.queueWaitingSendPacketsLength();
    }

    void standby();

private:

    LoraMesher& radio = LoraMesher::getInstance();

    TaskHandle_t receiveLoRaMessage_Handle = NULL;

    LoRaMeshService(): MessageService(appPort::LoRaMesherApp, String("LoRaMesherApp")) {
        commandService = loraMesherCommandService;
    };

    void createReceiveMessages();

    LoRaMeshMessage* createLoRaMeshMessage(DataMessage* message);

    DataMessage* createDataMessage(AppPacket<LoRaMeshMessage>* message);
};

