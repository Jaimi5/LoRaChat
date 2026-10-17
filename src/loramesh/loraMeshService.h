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

#include <functional>
#include <string>


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

    void updateRoutingTable();

    std::vector<loramesher::RouteEntry> getRoutingTableEntries();

    size_t GetRxQueueSize() const;

    size_t GetTxQueueSize() const;

private:
    bool running_ = false;

    SleepMode sleepMode_ = SleepMode::NONE;
    std::function<SleepBlockers()> sleepBlockers_;
    SleepDecision lastSleepDecision_ = SleepDecision::ALLOW;
    uint32_t lightSleepPlannedMs_ = 0;

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
