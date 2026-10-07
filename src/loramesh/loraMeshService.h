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

    // Human-readable summary of the radio parameters actually applied to the
    // LoRa stack at init (SF/BW/power/max packet size), captured in both the v1
    // and v2 paths. Used by the simulator to periodically log the live config so
    // a stale/ignored value is visible inside the measurement window.
    String getRadioInfo();

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

    // LoRaMesher 2.0.0 feature tests (see SIM_GROUP / SIM_RELIABLE in config.h).
    // Send `message` to SIM_GROUP_ADDR with ACK collection; logs GROUP_TX.
    bool sendGroup(DataMessage* message);

    // True once the node is in NORMAL_OPERATION or acting as Network Manager.
    bool isJoined();
#else
    void loopReceivedPackets();

    LM_LinkedList<RouteNode>* routingTableList = NULL;
#endif

private:
    // Radio config actually applied at init (version-agnostic). Populated by
    // initLoraMesherService() in both the v1 and v2 branches.
    String radioInfo_ = "SF=? BW=? pow=? maxPkt=?";

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

    // Common inbound path for SetDataCallback / SetDataCallbackEx. Group
    // traffic (dest in the group range) only logs GROUP_RX, so it never shows
    // up as APP_RX in the unicast PDR.
    void handleReceived(loramesher::AddressType source, loramesher::AddressType dest,
                        const uint8_t* data, size_t len);

    void onDelivery(const loramesher::LoraMesher::DeliveryResult& result);

    // MessageId -> app seq for reliable/group sends, so delivery outcomes can be
    // logged with the APP_TX seq. Written from the app task, read from the
    // protocol task's delivery callback.
    struct TrackedMessage {
        uint64_t key;
        uint32_t appSeq;
        bool group;
        bool used;
    };
    static constexpr size_t kMaxTracked = 16;
    TrackedMessage tracked_[kMaxTracked] = {};
    size_t trackedNext_ = 0;
    portMUX_TYPE trackedMux_ = portMUX_INITIALIZER_UNLOCKED;

    void trackMessage(uint64_t key, uint32_t appSeq, bool group);
    bool findTracked(uint64_t key, bool remove, uint32_t* appSeq, bool* group);

    // Once-a-second housekeeping: HEALTH line, SIM_STOPSTART and SIM_NM_FAILOVER_MS.
    TaskHandle_t diagTask_Handle = nullptr;
    static void diagLoop(void* pvParameters);
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
