#pragma once

#include <cstdint>
#include <string>

/** What a node report tells the server. */
enum class ReportEvent : uint8_t {
    /** A window found nothing to install. */
    NOOP,
    /** A new image was written and the node is about to reboot into it. */
    WRITTEN,
    /** The new image passed its self-test. */
    VALID,
    /** The bootloader rolled back from a failed image. */
    ROLLBACK,
    /** An update attempt failed before the reboot. */
    FAILED,
};

/** @return the wire name of @p event. */
const char* reportEventName(ReportEvent event);

/** Value of a numeric OtaReport field that is left out of the report. */
constexpr int64_t REPORT_ABSENT = INT64_MIN;

/**
 * @brief Node report sent with POST /report (schema v1).
 *
 * Empty strings and numbers equal to REPORT_ABSENT are left out of the encoded report.
 * node, env, ver, boot and event are always sent.
 */
struct OtaReport {
    uint16_t node = 0;
    std::string mac;
    std::string env;
    /** "gateway" or "sensor". */
    std::string role;
    /** esp_app_desc version of the running image. */
    std::string ver;
    /** Running partition label. */
    std::string part;
    /** OTA image state of the running partition, e.g. VALID. */
    std::string state;
    /** Boot counter kept in NVS. */
    uint32_t boot = 0;
    /** Reset reason. */
    std::string rr;
    /** Bootloader SHA-256 prefix in hex. */
    std::string bl;
    ReportEvent event = ReportEvent::NOOP;
    std::string lastInvalid;
    /** UpdateStatus code. */
    int64_t status = REPORT_ABSENT;
    /** PolicyDecision code. */
    int64_t decision = REPORT_ABSENT;
    /** FailReason code. */
    int64_t reason = REPORT_ABSENT;
    /** 8-byte prefix of the image SHA-256 the event refers to, 16 hex digits. */
    std::string sha;
    int64_t rssi = REPORT_ABSENT;
    int64_t dlMs = REPORT_ABSENT;
    int64_t bytes = REPORT_ABSENT;
    int64_t heapFree = REPORT_ABSENT;
    int64_t heapMin = REPORT_ABSENT;
    int64_t battMv = REPORT_ABSENT;
    /** 1 if USB or solar power is present, 0 if not, -1 to leave it out. */
    int8_t vbus = -1;
    int64_t uptimeS = REPORT_ABSENT;
};

/**
 * @return the report as compact JSON, fields in a fixed order. The server checks the
 *         signature over these exact bytes.
 */
std::string encodeReport(const OtaReport& report);
