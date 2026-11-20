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
     * @brief Runs /install: starts install mode for @p seconds, sets a new end time while it
     *        is on, or stops it with 0. A report follows at once. Field 0 of @p out gets the
     *        seconds of a started mode, field 1 the marker of a stopped one.
     * @return success, or ARG above INSTALL_MAX_SECONDS.
     */
    TypedResult install(uint32_t seconds, Fields& out);

    /** @return true while install mode is on. Any task may call it, it never blocks. */
    bool installActive() const;

    /** @return the seconds of install mode left, 0 when it is off. Any task may call it. */
    uint32_t installRemainingS() const;

    /**
     * @brief Writes the reply fields of /report: the interval in force in seconds, whether the
     *        configured interval is the default (0) or stored (1), the seconds until the next
     *        report and, while install mode shortens the interval, the configured interval.
     */
    TypedResult reportInto(Fields& out) const;

    /**
     * @brief Runs /report.set: @p seconds is the interval to store, or 0 to remove the stored
     *        one. The new interval applies at once and a report follows. Field 0 of @p out
     *        gets a stored interval, field 1 the default interval that is back in force.
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
