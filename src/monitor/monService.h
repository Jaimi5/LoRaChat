#pragma once
#include <Arduino.h>
#include <cstdint>
#include <vector>
#include "config.h"

#include "loramesher.hpp"

#include "installMode.h"
#include "message/messageManager.h"
#include "message/messageService.h"
#include "monCommandService.h"
#include "monReport.h"

/**
 * @brief Sends this node's state and routes to the MQTT server (MonApp): every
 *        MON_SENDING_EVERY ms, and every two superframes while install mode is on.
 */
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

    /**
     * @brief Runs /install: starts install mode for the seconds in @p args (default 600), sets
     *        a new end time while it is on, or stops it with 0. A report follows at once.
     * @return the reply for the command.
     */
    String install(const String& args);

    /** @return true while install mode is on. Any task may call it, it never blocks. */
    bool installActive() const;

    /** @return the seconds of install mode left, 0 when it is off. Any task may call it. */
    uint32_t installRemainingS() const;

private:
    MonService() : MessageService(MonApp, "Mon") { commandService = &commands_; }
    monCommandService commands_;
    void createSendingTask();
    static void sendingLoopOneMessage(void*);
    /** @return the routes this node reports: valid direct neighbours, or all valid routes. */
    static std::vector<MonRoute> collectReportedRoutes();

    /**
     * @return the node state of a report (uptime, queues, interval, power and install mode)
     *         without routes.
     */
    MonReport collectState() const;

    /** @return the time until the next report in ms. */
    uint32_t nextReportDelayMs() const;

    /**
     * Sends the node state with @p routes to MQTT in as many messages as one LoRa frame each
     * requires. A node without routes sends its state alone.
     */
    void sendReport(const std::vector<MonRoute>& routes);
    TaskHandle_t sending_TaskHandle = nullptr;
    bool running = false;
    uint8_t monMessageId = 0;
    InstallMode installMode_;
};
