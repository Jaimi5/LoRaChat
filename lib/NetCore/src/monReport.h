#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/** One reported route of a monitor report. */
struct MonRoute {
    uint16_t neighbor = 0;
    uint16_t nextHop = 0;
    uint8_t linkQuality = 0;
    uint8_t hopCount = 0;
};

/** Periodic routing report a node sends to the MQTT server. */
struct MonReport {
    uint32_t uptimeMs = 0;
    uint16_t txQueue = 0;
    uint16_t rxQueue = 0;
    std::vector<MonRoute> routes;
};

/** First field of every report payload; older firmware sent a route count there instead. */
constexpr uint16_t MON_REPORT_MARKER = 0xFFFF;
/** Marker, uptime, TX queue, RX queue and route count, little-endian. */
constexpr size_t MON_REPORT_HEADER_SIZE = 14;
/** Neighbour, next hop, link quality and hop count, little-endian. */
constexpr size_t MON_ROUTE_SIZE = 6;

/** @return the payload of @p report: the header followed by every route. */
std::vector<uint8_t> encodeMonReport(const MonReport& report);

/**
 * Decodes a report payload of @p size bytes into @p report.
 * @return false if the payload has no marker, is shorter than its header, or its route count
 *         does not match its size.
 */
bool decodeMonReport(const uint8_t* payload, size_t size, MonReport& report);

/** @return how many routes fit in a payload of at most @p maxPayload bytes (0 if none fits). */
size_t monRoutesPerMessage(size_t maxPayload);
