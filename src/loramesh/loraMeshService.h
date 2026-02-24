#pragma once

#include <Arduino.h>

#include "config.h"

#ifdef USE_LORAMESHER_V2
#include "loramesher.hpp"
#else
#include "LoraMesher.h"
#endif

#include "loraMeshMessage.h"

#include "message/messageManager.h"

#include "message/messageService.h"

#include "loraMeshCommandService.h"


class LoRaMeshService : public MessageService {
public:
    static LoRaMeshService& getInstance() {
        static LoRaMeshService instance;
        return instance;
    }

    ~LoRaMeshService() {
        if (loraMesherCommandService != nullptr) {
            delete loraMesherCommandService;
        }
    }

    void initLoraMesherService();

    uint16_t getLocalAddress();

    String getRoutingTable();

    void send(DataMessage* message);

    bool sendClosestGateway(DataMessage* message);

    void setGateway();

    void removeGateway();

    LoRaMeshCommandService* loraMesherCommandService = nullptr;

    bool hasActiveConnections();

    bool hasActiveSentConnections();

    bool hasActiveReceivedConnections();

    size_t queueWaitingSendPacketsLength();

    void standby();

    bool hasGateway();

    void updateRoutingTable();

#ifdef USE_LORAMESHER_V2
    std::vector<loramesher::RouteEntry> getRoutingTableEntries();
#else
    void loopReceivedPackets();

    LM_LinkedList<RouteNode>* routingTableList = NULL;
#endif

private:
#ifdef USE_LORAMESHER_V2
    std::unique_ptr<loramesher::LoraMesher> mesher_;
#else
    LoraMesher& radio = LoraMesher::getInstance();

    TaskHandle_t receiveLoRaMessage_Handle = NULL;

    void createReceiveMessages();

    DataMessage* createDataMessage(AppPacket<LoRaMeshMessage>* message);
#endif

    LoRaMeshService() : MessageService(appPort::LoRaMesherApp, String("LoRaMesherApp")) {
        loraMesherCommandService = new LoRaMeshCommandService();
        commandService = loraMesherCommandService;
    };

    LoRaMeshMessage* createLoRaMeshMessage(DataMessage* message);
};
