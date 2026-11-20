#pragma once
#include <Arduino.h>
#include <atomic>
#include <cstdint>
#include <vector>
#include "config.h"

#include "loramesher.hpp"

#include "installMode.h"
#include "message/messageManager.h"
#include "message/messageService.h"
#include "monCommandService.h"
#include "monReport.h"
#include "reportInterval.h"

/** Error of /report.set: the interval could not be stored. */
constexpr uint8_t MON_ERR_STORE = 128;

/**
 * @brief Sends this node's state and routes to the MQTT server (MonApp): every
 *        MON_SENDING_EVERY ms or the interval stored with /report.set, and every two
 *        superframes while install mode is on and that is shorter.
 */
class MonService : public MessageService {
public:
    static MonService& getInstance() {
        static MonService instance;
        return instance;
    }
    /** Loads the stored report interval and starts the sending task. NVS must be ready. */
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
    CommandReply install(const String& args);

    /** @return true while install mode is on. Any task may call it, it never blocks. */
    bool installActive() const;

    /** @return the seconds of install mode left, 0 when it is off. Any task may call it. */
    uint32_t installRemainingS() const;

    /**
     * @return the report interval in force, whether it is stored or the default, whether
     *         install mode shortens it, and the time until the next report.
     */
    String describeReport() const;

    /**
     * @brief Runs /report.set: stores the interval in @p args (seconds), or removes the stored
     *        one with "default" or 0. The new interval applies at once and a report follows.
     * @return the reply for the command.
     */
    CommandReply setReportInterval(const String& args);

    /**
     * @brief Runs /report.set with a typed argument: @p seconds is the interval, or 0 to remove
     *        the stored one. Then field 0 of @p out gets the default interval in seconds.
     * @return success, ARG for an interval out of range, or MON_ERR_STORE with the esp_err_t
     *         code.
     */
    TypedResult setReportInterval(uint32_t seconds, Fields& out);

private:
    /** Stores @p seconds and applies it at once. A report follows. */
    esp_err_t applyReportInterval(uint32_t seconds);

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

    /** @return the report interval in force and where it comes from. */
    ReportPacing reportPacing() const;

    /** @return the time until the next report in ms. */
    uint32_t nextReportDelayMs() const;

    /** Reads the stored report interval from NVS. Without one the default stays in force. */
    void loadReportInterval();

    /**
     * Sends the node state with @p routes to MQTT in as many messages as one LoRa frame each
     * requires. A node without routes sends its state alone.
     */
    void sendReport(const std::vector<MonRoute>& routes);
    TaskHandle_t sending_TaskHandle = nullptr;
    bool running = false;
    uint8_t monMessageId = 0;
    InstallMode installMode_;
    /** Report interval stored by the operator in seconds, REPORT_INTERVAL_NONE without one. */
    std::atomic<uint32_t> reportIntervalS_{REPORT_INTERVAL_NONE};
    /** Clock value at which the sending task sends its next report. */
    std::atomic<uint64_t> nextReportAtMs_{0};
};
