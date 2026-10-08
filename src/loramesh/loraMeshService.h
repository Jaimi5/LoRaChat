#pragma once

#include <Arduino.h>

#include "config.h"

#include "loramesher.hpp"

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

    /** Starts the mesh; @p networkManager makes this node the LoRaMesher network manager. */
    void initLoraMesherService(bool networkManager);

    uint16_t getLocalAddress();

    String getRoutingTable();

    void send(DataMessage* message);

    bool sendClosestGateway(DataMessage* message);

    void setGateway();

    void removeGateway();

    LoRaMeshCommandService* loraMesherCommandService = nullptr;

    void standby();

    /** @return true once the mesh stack has started. */
    bool isRunning() const { return running_; }

    /** @return true if this node has heard the mesh, i.e. it knows at least one other node. */
    bool hasMeshContact();

    /** @return the current TDMA superframe duration in ms, or 0 when unknown or not TDMA. */
    uint32_t getSuperframeDurationMs();

    bool hasGateway();

    void updateRoutingTable();

    std::vector<loramesher::RouteEntry> getRoutingTableEntries();

    size_t GetRxQueueSize() const;

    size_t GetTxQueueSize() const;

private:
    bool running_ = false;

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

    LoRaMeshService() : MessageService(appPort::LoRaMesherApp, String("LoRaMesherApp")) {
        loraMesherCommandService = new LoRaMeshCommandService();
        commandService = loraMesherCommandService;
    };

    LoRaMeshMessage* createLoRaMeshMessage(DataMessage* message);
};
