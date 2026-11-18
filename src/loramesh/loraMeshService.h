#pragma once

#include <Arduino.h>

#include "config.h"

#include "loramesher.hpp"

#include "loraMeshMessage.h"

#include "message/messageManager.h"

#include "message/messageService.h"
#include "message/ownedQueue.h"

#include "loraMeshCommandService.h"

#include "sleepMode.h"
#include "statusScreen.h"

#include <atomic>
#include <functional>
#include <string>


/** State of the mesh as the status screen shows it. */
struct MeshStatus {
    MeshState state = MeshState::INITIALIZING;
    bool synchronized = false;
    /** Nodes the mesh knows. */
    size_t connectedNodes = 0;
};

class LoRaMeshService : public MessageService {
public:
    static LoRaMeshService& getInstance() {
        static LoRaMeshService instance;
        return instance;
    }

    /**
     * @brief Starts the mesh.
     * @param networkManager this node is the LoRaMesher network manager (it never sleeps).
     * @param sleepMode how the MCU of a sensor sleeps through the SLEEP slots.
     * @param sleepFractionPercent minimum share of the superframe the network sleeps (used by the
     *        network manager), or -1 for LORA_MIN_SLEEP_FRACTION.
     */
    void initLoraMesherService(bool networkManager, SleepMode sleepMode, int sleepFractionPercent);

    /**
     * @brief Sets what is asked, on the mesh task, whenever the mesh offers the MCU a sleep.
     *        It must only read state, never block. Call before initLoraMesherService().
     */
    void setSleepBlockers(std::function<SleepBlockers()> blockers) {
        sleepBlockers_ = std::move(blockers);
    }

    /** @return the light and deep sleeps (count and planned time) since power-on. */
    std::string sleepReport() const;

    /**
     * @brief Releases the pin levels held through a deep sleep. Call first in setup(): until
     *        then the serial port, I2C and the radio pins stay frozen.
     */
    static void releaseDeepSleepPadHold();

    uint16_t getLocalAddress();

    /**
     * @return the largest payload one service message can carry in a single LoRa packet with
     *         the configured spreading factor and bandwidth (LoRaMesher caps the packet size).
     */
    size_t maxAppPayload() const;

    String getRoutingTable();

    void send(DataMessage* message);

    bool sendClosestGateway(DataMessage* message);

    void setGateway();

    void removeGateway();

    void standby();

    /** @return true once the mesh stack has started. */
    bool isRunning() const { return running_; }

    /** @return true if this node has heard the mesh, i.e. it knows at least one other node. */
    bool hasMeshContact();

    /** @return the current TDMA superframe duration in ms, or 0 when unknown or not TDMA. */
    uint32_t getSuperframeDurationMs();

    bool hasGateway();

    /**
     * @return false if the mesh is not running or its routing table has no active route to
     *         @p dst. A route to a node that sleeps or moved away stays active until its
     *         routing messages have been missed for long enough.
     */
    bool hasRouteTo(uint16_t dst);

    void updateRoutingTable();

    std::vector<loramesher::RouteEntry> getRoutingTableEntries();

    /** @return the state of the mesh. Any task may call it, also before the mesh starts. */
    MeshStatus getMeshStatus();

    /**
     * @param nextHop the neighbour that relays toward the closest gateway.
     * @param hops the hops to that gateway.
     * @return false if the node knows no gateway. Any task may call it.
     */
    bool getGatewayRoute(uint16_t& nextHop, uint8_t& hops);

    /**
     * @param ageMs milliseconds since the last message for this node arrived from the mesh.
     * @return false if none has arrived since boot. Any task may call it.
     */
    bool getLastRxAgeMs(uint32_t& ageMs) const;

    /**
     * @brief Tells how long ago this node last heard the mesh: a message for it, or any packet
     *        that refreshed the route of a direct neighbour in @p routes.
     * @param ageMs milliseconds since then.
     * @return false if the node has heard nothing since boot. Any task may call it.
     */
    bool getLastHeardAgeMs(const std::vector<loramesher::RouteEntry>& routes, uint32_t& ageMs);

    size_t GetRxQueueSize() const;

    size_t GetTxQueueSize() const;

private:
    /** Set after mesher_ exists and has started, so other tasks may read through it. */
    std::atomic<bool> running_{false};
    /** Clock of the last received message in ms, 0 while there is none. */
    std::atomic<uint32_t> lastRxMs_{0};
    /** Clock of the last packet heard from the mesh in ms, 0 while there is none. */
    std::atomic<uint32_t> lastHeardMs_{0};

    SleepMode sleepMode_ = SleepMode::NONE;
    std::function<SleepBlockers()> sleepBlockers_;
    SleepDecision lastSleepDecision_ = SleepDecision::ALLOW;
    uint32_t lightSleepPlannedMs_ = 0;
#if PMU_KEY_WAKES_LIGHT_SLEEP
    /** Light sleeps that the power key ended before their timer. */
    std::atomic<uint32_t> keyWakes_{0};
#endif

    /** Books a deep sleep that ended in this boot into the sleep counters. */
    static void countDeepSleepWake();


    loramesher::power::SleepResult onPrepareSleep(const loramesher::power::SleepContext& context);
    void onWakeUp(loramesher::power::PowerState previous);

    std::unique_ptr<loramesher::LoraMesher> mesher_;

    /** Messages received from the mesh, waiting for the receive task. */
    OwnedQueue<DataMessage, DataMessageDeleter> loraReceiveQueue_;
    TaskHandle_t loraReceiveTask_Handle = nullptr;

    static void loraReceiveLoop(void* pvParameters);
    void createReceiveTask();

    LoRaMeshService() : MessageService(appPort::LoRaMesherApp, String("LoRaMesherApp")) {
        commandService = &commands_;
    }

    LoRaMeshCommandService commands_;

    /** @return the LoRa frame of @p message: its ports, its id and its payload. */
    static std::vector<uint8_t> createLoRaMeshFrame(const DataMessage& message);
};
