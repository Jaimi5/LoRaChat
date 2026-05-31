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

    // Largest known hop_count in the routing table (>=1, version-agnostic). Used
    // by senders to size how long a packet needs to traverse the mesh.
    uint8_t getMaxHopDepth();

    // Pace a sender to the LoRaMesher TDMA schedule: block until `nSlots` of this
    // node's data slots have passed. On v2 each step waits getTimeUntilNextDataSlot()
    // (falling back to fallbackMs before the node has joined / on v1).
    void waitForDataSlots(uint8_t nSlots, uint32_t fallbackMs);

#ifdef USE_LORAMESHER_V2
    std::vector<loramesher::RouteEntry> getRoutingTableEntries();

    size_t GetRxQueueSize() const;

    size_t GetTxQueueSize() const;

    uint32_t getTimeUntilNextDataSlot(uint32_t guard_time_ms = 200) const;
#else
    void loopReceivedPackets();

    LM_LinkedList<RouteNode>* routingTableList = NULL;
#endif

private:
#ifdef USE_LORAMESHER_V2
    std::unique_ptr<loramesher::LoraMesher> mesher_;

    // Queue message struct — heap-allocated pointer, freed after processing
    struct LoRaQueueMessage {
        loramesher::AddressType source;
        DataMessage* dataMessage;  // pvPortMalloc'd
    };

    QueueHandle_t loraReceiveQueue_ = nullptr;
    TaskHandle_t loraReceiveTask_Handle = nullptr;

    static void loraReceiveLoop(void* pvParameters);
    void createReceiveTask();
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
