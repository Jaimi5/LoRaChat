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
    /** Last RSSI of a direct neighbour in dBm, -127 to -1. 0 means RSSI and SNR are unknown. */
    int8_t rssi = 0;
    /** Last SNR of a direct neighbour in steps of 0.25 dB. Meaningful only when rssi is not 0. */
    int8_t snrQuarterDb = 0;
};

/** Humidity of a sensor that measures none. */
constexpr uint16_t MON_NO_HUMIDITY = 0xFFFF;

/** Optional readings of an environment sensor. */
struct MonSensor {
    /** Temperature in 0.01 °C. */
    int16_t tempCentiC = 0;
    /** Relative humidity in 0.01 %, or MON_NO_HUMIDITY. */
    uint16_t rhCentiPercent = MON_NO_HUMIDITY;
    /** Pressure in 0.1 hPa. */
    uint16_t pressureDeciHpa = 0;
};

/** Periodic report a node sends to the MQTT server: its state and its routes. */
struct MonReport {
    /** Format of the decoded payload, 1 or 2. Format 1 carries only uptime, queues and routes. */
    uint8_t version = 2;
    /** Time since boot. Format 2 carries whole seconds. */
    uint64_t uptimeMs = 0;
    /** Queue lengths. Format 2 carries at most 255. */
    uint16_t txQueue = 0;
    uint16_t rxQueue = 0;
    /** Time between two reports in seconds. */
    uint16_t intervalS = 0;
    /** Battery voltage in mV, 0 when unknown. */
    uint16_t battMv = 0;
    bool externalPower = false;
    bool charging = false;
    bool batteryPresent = false;
    bool installMode = false;
    /** The node understands binary command frames (cmdFrame.h). Format 2 only. */
    bool binaryCommands = false;
    bool hasSensor = false;
    MonSensor sensor;
    std::vector<MonRoute> routes;
};

/** First field of a format 1 payload. Older firmware sent a route count there. */
constexpr uint16_t MON_REPORT_MARKER = 0xFFFF;
/** First field of a format 2 payload. */
constexpr uint16_t MON_REPORT_MARKER_V2 = 0xFFFE;

/** Format 1: marker, uptime, TX queue, RX queue and route count, little-endian. */
constexpr size_t MON_REPORT_HEADER_SIZE_V1 = 14;
/** Format 1: neighbour, next hop, link quality and hop count, little-endian. */
constexpr size_t MON_ROUTE_SIZE_V1 = 6;

/**
 * Format 2 header, little-endian: marker u16, flags u8, uptime in seconds u32, TX queue u8,
 * RX queue u8, interval in seconds u16, battery in mV u16 and route count u8.
 */
constexpr size_t MON_REPORT_HEADER_SIZE = 14;
/** Format 2 sensor block after the header: temperature i16, humidity u16 and pressure u16. */
constexpr size_t MON_SENSOR_SIZE = 6;
/** Format 2 route: neighbour u16, next hop u16, link quality u8, hop count u8, RSSI i8, SNR i8. */
constexpr size_t MON_ROUTE_SIZE = 8;
/** The route count of a format 2 payload is one byte. */
constexpr size_t MON_MAX_ROUTES = 255;

/** Bits of the format 2 flags byte. A decoder ignores the bits it does not know. */
constexpr uint8_t MON_FLAG_EXTERNAL_POWER = 1U << 0;
constexpr uint8_t MON_FLAG_CHARGING = 1U << 1;
constexpr uint8_t MON_FLAG_BATTERY_PRESENT = 1U << 2;
constexpr uint8_t MON_FLAG_SENSOR = 1U << 3;
constexpr uint8_t MON_FLAG_INSTALL_MODE = 1U << 4;
constexpr uint8_t MON_FLAG_BINARY_COMMANDS = 1U << 5;

/**
 * @return the format 2 payload of @p report: the header, the sensor block if it has one, and
 *         its routes. Values that do not fit their field are saturated and at most
 *         MON_MAX_ROUTES routes are written.
 */
std::vector<uint8_t> encodeMonReport(const MonReport& report);

/** @return the format 1 payload of @p report, as older firmware sends it. */
std::vector<uint8_t> encodeMonReportV1(const MonReport& report);

/**
 * Decodes a format 1 or format 2 payload of @p size bytes into @p report.
 * @return false if the payload has no known marker, is shorter than its header, or its route
 *         count and sensor flag do not match its size.
 */
bool decodeMonReport(const uint8_t* payload, size_t size, MonReport& report);

/**
 * @return how many routes fit in a format 2 payload of at most @p maxPayload bytes, with or
 *         without the sensor block (0 if none fits).
 */
size_t monRoutesPerMessage(size_t maxPayload, bool hasSensor);

/**
 * Splits @p report into reports whose payloads each fit in @p maxPayload bytes. Every part
 * repeats the node state and only the first one carries the sensor block. A report without
 * routes gives one part.
 * @return the parts, or none if @p maxPayload is too small for the report.
 */
std::vector<MonReport> splitMonReport(const MonReport& report, size_t maxPayload);

/** @return @p dbm as a route RSSI: 0 for an unknown value (0 or NaN), otherwise -127 to -1. */
int8_t monPackRssi(float dbm);

/** @return @p db in steps of 0.25 dB, rounded to the nearest step and limited to -32 to 31.75 dB.
 */
int8_t monPackSnr(float db);

/** @return the SNR in dB of a route's snrQuarterDb. */
float monSnrDb(int8_t quarterDb);

/** @return the uptime of @p report in milliseconds, limited to what 32 bits hold. */
uint32_t monUptimeMs(const MonReport& report);
