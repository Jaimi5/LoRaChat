#pragma once

#include <cstdint>
#include <string>

/** Shortest monitor report interval an operator may store, in seconds. */
constexpr uint32_t REPORT_INTERVAL_MIN_S = 60;
/** Longest monitor report interval an operator may store, in seconds. */
/** 12 hours. The report carries its interval in 16 bits of seconds, so it must stay below 65536. */
constexpr uint32_t REPORT_INTERVAL_MAX_S = 43200;
/** Stored value that stands for "no interval stored". */
constexpr uint32_t REPORT_INTERVAL_NONE = 0;

/**
 * @brief Reads the argument of /report.set: whole seconds from REPORT_INTERVAL_MIN_S to
 *        REPORT_INTERVAL_MAX_S in digits, or "default" (any case) or "0" to remove the stored
 *        interval. Spaces around the argument are ignored.
 * @param seconds the interval, or REPORT_INTERVAL_NONE to remove the stored one.
 * @return false, leaving @p seconds unchanged, for anything else.
 */
bool parseReportInterval(const std::string& args, uint32_t& seconds);

/** @return true if @p seconds is an interval an operator may store. */
bool reportIntervalValid(uint32_t seconds);

/** The time between two monitor reports and where it comes from. */
struct ReportPacing {
    /** Interval the node keeps outside install mode, in ms. */
    uint32_t configuredMs = 0;
    /** The configured interval is the stored one. Otherwise it is the firmware default. */
    bool stored = false;
    /** Interval in force, in ms. */
    uint32_t intervalMs = 0;
    /** Install mode is on and its interval is shorter than the configured one. */
    bool installShortens = false;
};

/**
 * @brief Chooses the interval in force.
 * @param defaultMs interval of the firmware, in ms.
 * @param storedS interval stored by the operator in seconds. A value that is not valid, such
 *        as REPORT_INTERVAL_NONE, selects the default.
 * @param installActive install mode is on.
 * @param installIntervalMs interval of install mode, in ms. Install mode never lengthens the
 *        configured interval.
 */
ReportPacing chooseReportPacing(uint32_t defaultMs, uint32_t storedS, bool installActive,
                                uint32_t installIntervalMs);

/**
 * @brief The reply of /report.
 * @param nextInS seconds until the next report.
 * @return for example "report every 300 s (default), next in 212 s", or in install mode
 *         "report every 20 s (install mode, then 600 s stored), next in 12 s".
 */
std::string describeReportPacing(const ReportPacing& pacing, uint32_t nextInS);
