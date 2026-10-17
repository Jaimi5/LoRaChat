#pragma once

#include <cstdint>
#include <string>

#include "otaRecord.h"
#include "otaReport.h"

/** Fields of a report that depend on the event; unset fields are not sent. */
struct ReportDetails {
    int status = -1;
    int decision = -1;
    int reason = -1;
    /** Image SHA-256 prefix in hex (16 digits). */
    std::string sha;
    std::string lastInvalid;
    bool download = false;
    uint32_t downloadMs = 0;
    uint32_t bytes = 0;
};

/**
 * @brief Reports update events to the OTA server: POST /report on the host of the OTA server,
 *        signed with the report key derived from the deployment key (header X-LM-Tag).
 *
 * Needs a joined WiFi network. The verdict of an update attempt is only known after the reboot;
 * it is sent at the next window and remembered in NVS once the server has it.
 */
class OtaReporter {
public:
    /** Sends @p event. @return true if the server stored the report. */
    static bool send(ReportEvent event, const ReportDetails& details);

    /** Sends the verdict (valid or rollback) of the last update attempt if it was not sent yet. */
    static void sendPendingVerdict();

    /**
     * @brief On a gateway: waits in a task until the boot guard has decided and the WiFi has an
     *        address, then sends the pending verdict. Sensor nodes send it in their windows.
     */
    static void sendVerdictWhenConnected();

    /** @return @p sha in hex, as the report's "sha" field. */
    static std::string shaHex(const ShaPrefix& sha);

private:
    /** @return the HTTP status, or -1 if the server could not be reached. */
    static int post(const std::string& body);
    static std::string reportUrl();
};
