#pragma once
#include <Arduino.h>
#include <cstdint>
#include <vector>
#include "config.h"

#include "loramesher.hpp"

#include "message/messageManager.h"
#include "message/messageService.h"
#include "monCommandService.h"
#include "monReport.h"

/** Sends this node's routes to the MQTT server every MON_SENDING_EVERY ms (MonApp). */
class MonService : public MessageService {
public:
    static MonService& getInstance() {
        static MonService instance;
        return instance;
    }
    void init();
    /** Turns a report received from any node into its JSON for the server. */
    String getJSON(DataMessage* message) override;
    /** Reports only travel to the server; none are accepted from it. */
    DataMessagePtr getDataMessage(JsonObject data) override;
    void processReceivedMessage(messagePort port, DataMessage* message) override;

private:
    MonService() : MessageService(MonApp, "Mon") { commandService = &commands_; }
    monCommandService commands_;
    void createSendingTask();
    static void sendingLoopOneMessage(void*);
    /** @return the routes this node reports: valid direct neighbours, or all valid routes. */
    static std::vector<MonRoute> collectReportedRoutes();

    /** Sends @p routes to MQTT in as many messages as one LoRa frame each requires. */
    void sendRoutes(const std::vector<MonRoute>& routes);
    TaskHandle_t sending_TaskHandle = nullptr;
    bool running = false;
    bool isCreated = false;
    uint8_t monMessageId = 0;
};
